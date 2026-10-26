#pragma once

#include <string>

namespace grpc_authz_fuzz::engine {

// placeholder for the future fuzz engine library surface
struct EngineInfo {
  static std::string Name() { return "grpc-authz-fuzz"; }
  static std::string Version() { return "0.1.0"; }
};

}  // namespace grpc_authz_fuzz::engine
