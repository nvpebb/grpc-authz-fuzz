#pragma once

#include <string>

#include "src/invoker/rpc_invoker.h"

namespace grpc_authz_fuzz::engine {

// worker process entry: reads WorkerJob from stdin, writes WorkerResult to stdout
// coverage is recorded into the named POSIX SHM region
int RunWorkerLoop(invoker::RpcInvoker* invoker, const std::string& shm_name);

}  // namespace grpc_authz_fuzz::engine
