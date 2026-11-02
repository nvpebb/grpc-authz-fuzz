#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sequence.pb.h"

namespace grpc_authz_fuzz::invoker {

struct StepResult {
  uint32_t step_index = 0;
  int status_code = 0;  // gRPC status; non-OK is still an RPC outcome
  std::string status_message;
  std::string response_type;
  std::string response;
};

struct ExecutionResult {
  std::vector<StepResult> steps;
  std::string error;  // invalid input or executor failure
};

class RpcInvoker {
 public:
  virtual ~RpcInvoker() = default;

  virtual bool Reset(std::string* error) = 0;
  virtual ExecutionResult Execute(const Sequence& sequence) = 0;
};

}  // namespace grpc_authz_fuzz::invoker
