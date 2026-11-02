#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <google/protobuf/message.h>

namespace grpc_authz_fuzz::engine {

enum class IoStatus { PENDING, COMPLETE, ERROR };

// Incremental reader for nonblocking pool pipes; a partial frame must not stall other workers.
class MessageReader {
 public:
  IoStatus Read(int fd, google::protobuf::Message* message);
  void Reset();

 private:
  std::array<uint8_t, 4> header_{};
  std::size_t header_read_ = 0;
  std::string payload_;
  std::size_t payload_read_ = 0;
};

class MessageWriter {
 public:
  bool Prepare(const google::protobuf::Message& message);
  IoStatus Write(int fd);
  void Reset();

 private:
  std::string frame_;
  std::size_t written_ = 0;
};

// Timed writes require a nonblocking fd; pool pipe ends are nonblocking.
bool WriteMessage(int fd, const google::protobuf::Message& message, int timeout_ms = -1);
bool ReadMessage(int fd, google::protobuf::Message* message, int timeout_ms = -1);

bool WriteAll(int fd, const void* data, std::size_t n);
bool ReadAll(int fd, void* data, std::size_t n);

}  // namespace grpc_authz_fuzz::engine
