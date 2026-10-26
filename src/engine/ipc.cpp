#include "src/engine/ipc.h"

#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <vector>

namespace grpc_authz_fuzz::engine {

bool WriteAll(int fd, const void* data, std::size_t n) {

  const auto* p = static_cast<const uint8_t*>(data);
  std::size_t left = n;

  while (left > 0) {

    const ssize_t w = ::write(fd, p, left);

    if (w < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }

    if (w == 0) {
      return false;
    }

    p += static_cast<std::size_t>(w);
    left -= static_cast<std::size_t>(w);
  }

  return true;
}

bool ReadAll(int fd, void* data, std::size_t n) {

  auto* p = static_cast<uint8_t*>(data);
  std::size_t left = n;

  while (left > 0) {

    const ssize_t r = ::read(fd, p, left);

    if (r < 0) {
      if (errno == EINTR) {
        continue;
      }

      return false;
    }

    if (r == 0) {
      return false;  // EOF
    }

    p += static_cast<std::size_t>(r);
    left -= static_cast<std::size_t>(r);
  }

  return true;
}

bool WriteMessage(int fd, const google::protobuf::Message& message) {

  std::string bytes;

  if (!message.SerializeToString(&bytes)) {
    return false;
  }

  if (bytes.size() > 0xffffffffu) {
    return false;
  }

  const uint32_t len = static_cast<uint32_t>(bytes.size());
  uint8_t header[4] = {
      static_cast<uint8_t>(len & 0xffu),
      static_cast<uint8_t>((len >> 8) & 0xffu),
      static_cast<uint8_t>((len >> 16) & 0xffu),
      static_cast<uint8_t>((len >> 24) & 0xffu),
  };

  return WriteAll(fd, header, sizeof(header)) && WriteAll(fd, bytes.data(), bytes.size());

}

bool ReadMessage(int fd, google::protobuf::Message* message) {

  uint8_t header[4];
  if (!ReadAll(fd, header, sizeof(header))) {
    return false;
  }

  const uint32_t len = static_cast<uint32_t>(header[0]) | (static_cast<uint32_t>(header[1]) << 8) |
                       (static_cast<uint32_t>(header[2]) << 16) |
                       (static_cast<uint32_t>(header[3]) << 24);

  // hard cap to avoid pathological allocations from a corrupted peer
  constexpr uint32_t kMaxMessageBytes = 16u * 1024u * 1024u;
  if (len > kMaxMessageBytes) {
    return false;
  }

  std::vector<char> buf(len);

  if (len > 0 && !ReadAll(fd, buf.data(), buf.size())) {
    return false;
  }
  
  return message->ParseFromArray(buf.data(), static_cast<int>(buf.size()));
}

}  // namespace grpc_authz_fuzz::engine
