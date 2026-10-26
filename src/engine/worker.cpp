#include "src/engine/worker.h"

#include <spdlog/spdlog.h>

#include "src/engine/coverage_shm.h"
#include "src/engine/ipc.h"
#include "worker.pb.h"

namespace grpc_authz_fuzz::engine {

int RunWorkerLoop(invoker::RpcInvoker* invoker, const std::string& shm_name) {
  if (invoker == nullptr) {
    spdlog::error("worker: invoker is null");
    return 1;
  }

  CoverageShm shm;
  try {
    shm = CoverageShm::Open(shm_name);
  } catch (const std::exception& ex) {
    spdlog::error("worker: {}", ex.what());
    return 1;
  }

  // stdin=0 jobs, stdout=1 results
  constexpr int kJobFd = 0;
  constexpr int kResultFd = 1;

  for (;;) {
    WorkerJob job;
    if (!ReadMessage(kJobFd, &job)) {
      break;  // parent closed pipe => clean exit
    }

    WorkerResult result;
    result.set_job_id(job.job_id());

    shm.Clear();
    invoker->Reset();

    for (const auto& step : job.sequence().steps()) {
      shm.ObserveMethod(step.method());
    }

    std::string error;
    if (!invoker->Execute(job.sequence(), &error)) {
      result.set_status(WorkerResult::TARGET_ERROR);
      result.set_error_message(error);
      result.set_steps_executed(0);
    } else {
      result.set_status(WorkerResult::OK);
      result.set_steps_executed(static_cast<uint32_t>(job.sequence().steps_size()));
    }
    result.set_coverage_bits_set(shm.CountSetBits());

    if (!WriteMessage(kResultFd, result)) {
      spdlog::error("worker: failed to write result for job {}", job.job_id());
      return 1;
    }
  }

  return 0;
}

}  // namespace grpc_authz_fuzz::engine
