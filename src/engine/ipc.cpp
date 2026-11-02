#include "src/engine/ipc.h"

#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <vector>

namespace grpc_authz_fuzz::engine {
namespace {

using Clock = std::chrono::steady_clock;
constexpr uint32_t kMaxMessageBytes = 16u * 1024u * 1024u;

// Block SIGPIPE only in the calling thread, without changing the application's handlers.
class BlockSigpipe {
 public:
  BlockSigpipe() {
    sigemptyset(&mask_);
    sigaddset(&mask_, SIGPIPE);
    active_ = pthread_sigmask(SIG_BLOCK, &mask_, &old_) == 0;
    sigset_t pending;
    sigpending(&pending);
    pending_ = sigismember(&pending, SIGPIPE) == 1;
  }

  ~BlockSigpipe() {
    if (active_) {
      if (!pending_) {
        const timespec timeout{};
        while (sigtimedwait(&mask_, nullptr, &timeout) < 0 && errno == EINTR) {}
      }
      pthread_sigmask(SIG_SETMASK, &old_, nullptr);
    }
  }

 private:
  sigset_t mask_{};
  sigset_t old_{};
  bool active_ = false;
  bool pending_ = false;
};

bool Transfer(int fd, void* data, std::size_t n, bool writing, Clock::time_point deadline) {
  auto* p = static_cast<uint8_t*>(data);
  while (n > 0) {
    int timeout = -1;
    if (deadline != Clock::time_point::max()) {
      const auto left = std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now()).count();
      if (left <= 0) {
        return false;
      }
      timeout = static_cast<int>(left);
    }
    pollfd ready{fd, static_cast<short>(writing ? POLLOUT : POLLIN), 0};
    const int r = poll(&ready, 1, timeout);
    if (r < 0 && errno == EINTR) {
      continue;
    }
    if (r <= 0) {
      return false;
    }
    const ssize_t count = writing ? ::write(fd, p, n) : ::read(fd, p, n);
    if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
      continue;
    }
    if (count <= 0) {
      return false;
    }
    p += count;
    n -= static_cast<std::size_t>(count);
  }
  return true;
}

Clock::time_point Deadline(int timeout_ms) {
  return timeout_ms < 0 ? Clock::time_point::max()
                        : Clock::now() + std::chrono::milliseconds(timeout_ms);
}

}  // namespace

void MessageReader::Reset() {
  header_read_ = 0;
  payload_.clear();
  payload_read_ = 0;
}

IoStatus MessageReader::Read(int fd, google::protobuf::Message* message) {
  for (;;) {
    const bool header = header_read_ < header_.size();
    if (!header && payload_read_ == payload_.size()) {
      const bool ok = message->ParseFromArray(payload_.data(), static_cast<int>(payload_.size()));
      Reset();
      return ok ? IoStatus::COMPLETE : IoStatus::ERROR;
    }
    void* data = header ? static_cast<void*>(header_.data() + header_read_)
                        : static_cast<void*>(payload_.data() + payload_read_);
    const std::size_t left = header ? header_.size() - header_read_
                                    : payload_.size() - payload_read_;
    const ssize_t n = ::read(fd, data, left);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return IoStatus::PENDING;
    }
    if (n <= 0) {
      return IoStatus::ERROR;
    }
    if (header) {
      header_read_ += static_cast<std::size_t>(n);
      if (header_read_ == header_.size()) {
        const uint32_t len = static_cast<uint32_t>(header_[0]) |
                             (static_cast<uint32_t>(header_[1]) << 8) |
                             (static_cast<uint32_t>(header_[2]) << 16) |
                             (static_cast<uint32_t>(header_[3]) << 24);
        if (len > kMaxMessageBytes) {
          return IoStatus::ERROR;
        }
        payload_.resize(len);
      }
    } else {
      payload_read_ += static_cast<std::size_t>(n);
    }
  }
}

void MessageWriter::Reset() {
  frame_.clear();
  written_ = 0;
}

bool MessageWriter::Prepare(const google::protobuf::Message& message) {
  Reset();
  std::string bytes;
  if (!message.SerializeToString(&bytes) || bytes.size() > kMaxMessageBytes) {
    return false;
  }
  const uint32_t len = static_cast<uint32_t>(bytes.size());
  for (unsigned int shift = 0; shift < 32; shift += 8) {
    frame_.push_back(static_cast<char>((len >> shift) & 0xffu));
  }
  frame_ += bytes;
  return true;
}

IoStatus MessageWriter::Write(int fd) {
  BlockSigpipe guard;
  while (written_ < frame_.size()) {
    const ssize_t n = ::write(fd, frame_.data() + written_, frame_.size() - written_);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return IoStatus::PENDING;
    }
    if (n <= 0) {
      return IoStatus::ERROR;
    }
    written_ += static_cast<std::size_t>(n);
  }
  Reset();
  return IoStatus::COMPLETE;
}

bool WriteAll(int fd, const void* data, std::size_t n) {
  BlockSigpipe guard;
  return Transfer(fd, const_cast<void*>(data), n, true, Clock::time_point::max());
}

bool ReadAll(int fd, void* data, std::size_t n) {
  return Transfer(fd, data, n, false, Clock::time_point::max());
}

bool WriteMessage(int fd, const google::protobuf::Message& message, int timeout_ms) {
  std::string bytes;
  if (!message.SerializeToString(&bytes) || bytes.size() > kMaxMessageBytes) {
    return false;
  }
  const uint32_t len = static_cast<uint32_t>(bytes.size());
  uint8_t header[4] = {
      static_cast<uint8_t>(len & 0xffu),
      static_cast<uint8_t>((len >> 8) & 0xffu),
      static_cast<uint8_t>((len >> 16) & 0xffu),
      static_cast<uint8_t>((len >> 24) & 0xffu),
  };
  BlockSigpipe guard;
  const auto deadline = Deadline(timeout_ms);
  return Transfer(fd, header, sizeof(header), true, deadline) &&
         Transfer(fd, bytes.data(), bytes.size(), true, deadline);
}

bool ReadMessage(int fd, google::protobuf::Message* message, int timeout_ms) {
  const auto deadline = Deadline(timeout_ms);
  uint8_t header[4];
  if (!Transfer(fd, header, sizeof(header), false, deadline)) {
    return false;
  }
  const uint32_t len = static_cast<uint32_t>(header[0]) | (static_cast<uint32_t>(header[1]) << 8) |
                       (static_cast<uint32_t>(header[2]) << 16) |
                       (static_cast<uint32_t>(header[3]) << 24);
  if (len > kMaxMessageBytes) {
    return false;
  }
  std::vector<char> buf(len);
  return Transfer(fd, buf.data(), buf.size(), false, deadline) &&
         message->ParseFromArray(buf.data(), static_cast<int>(buf.size()));
}

}  // namespace grpc_authz_fuzz::engine
