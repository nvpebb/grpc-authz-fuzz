#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <google/protobuf/message.h>

namespace grpc_authz_fuzz::engine {

bool WriteMessage(int fd, const google::protobuf::Message& message);
bool ReadMessage(int fd, google::protobuf::Message* message);

bool WriteAll(int fd, const void* data, std::size_t n);
bool ReadAll(int fd, void* data, std::size_t n);

}  // namespace grpc_authz_fuzz::engine
