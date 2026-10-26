#pragma once

#include <string>

#include "sequence.pb.h"

namespace grpc_authz_fuzz::invoker {

class RpcInvoker {
 public:
  virtual ~RpcInvoker() = default;

  virtual void Reset() = 0;

  virtual bool Execute(const Sequence& sequence, std::string* error) = 0;
};

}  // namespace grpc_authz_fuzz::invoker
