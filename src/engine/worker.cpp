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
    std::string error;
    const bool reset_ok = invoker->Reset(&error);
    if (!reset_ok) {
      result.set_status(WorkerResult::INTERNAL_ERROR);
      result.set_error_message(error);
    } else {
      const auto execution = invoker->Execute(job.sequence());
      result.set_status(execution.error.empty() ? WorkerResult::OK : WorkerResult::TARGET_ERROR);
      result.set_error_message(execution.error);
      result.set_steps_executed(static_cast<uint32_t>(execution.steps.size()));
      for (const auto& step : execution.steps) {
        shm.ObserveMethod(job.sequence().steps(static_cast<int>(step.step_index)).method());
        auto* outcome = result.add_outcomes();
        outcome->set_step_index(step.step_index);
        outcome->set_status_code(step.status_code);
        outcome->set_status_message(step.status_message);
        outcome->set_response_type(step.response_type);
        outcome->set_response(step.response);
      }
    }
    result.set_coverage_slots_set(shm.CountOccupiedSlots());

    if (!WriteMessage(kResultFd, result)) {
      spdlog::error("worker: failed to write result for job {}", job.job_id());
      return 1;
    }
    if (!reset_ok) {
      return 1;  // never reuse a worker after a failed reset
    }
  }

  return 0;
}

}  // namespace grpc_authz_fuzz::engine
