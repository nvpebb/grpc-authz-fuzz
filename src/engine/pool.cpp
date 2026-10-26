#include "src/engine/pool.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "src/engine/coverage_shm.h"
#include "src/engine/ipc.h"
#include "worker.pb.h"

namespace grpc_authz_fuzz::engine {
namespace {

struct WorkerSlot {
  int id = 0;
  int generation = 0;
  pid_t pid = -1;
  int to_worker = -1;    // parent write => worker stdin
  int from_worker = -1;  // parent read <= worker stdout
  CoverageShm shm;
  int jobs_since_spawn = 0;
};

void CloseFd(int& fd) {
  if (fd >= 0) {
    ::close(fd);
    fd = -1;
  }
}

std::string SelfExe() {

  std::array<char, 4096> buf{};
  const ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size() - 1);

  if (n < 0) {
    throw std::runtime_error(std::string("readlink(/proc/self/exe): ") + std::strerror(errno));
  }

  return std::string(buf.data(), static_cast<std::size_t>(n));
}

void KillWorker(WorkerSlot& slot) {

  if (slot.pid > 0) {
    ::kill(slot.pid, SIGKILL);
    int status = 0;
    ::waitpid(slot.pid, &status, 0);
    slot.pid = -1;
  }

  CloseFd(slot.to_worker);
  CloseFd(slot.from_worker);
}

void SpawnWorker(WorkerSlot& slot, const PoolConfig& config) {
  KillWorker(slot);

  ++slot.generation;
  // unique name per spawn, reusing the same path races with CoverageShm dtor unlink
  const std::string shm_name = config.shm_prefix + "_" + std::to_string(getpid()) + "_" +
                               std::to_string(slot.id) + "_" + std::to_string(slot.generation);
  slot.shm = CoverageShm::Create(shm_name);

  int in_pipe[2] = {-1, -1};   // parent writes to [1], child reads [0]
  int out_pipe[2] = {-1, -1};  // child writes to [1], parent reads [0]
  // O_CLOEXEC: sibling workers must not inherit these ends, or stdin EOF never fires
  if (pipe2(in_pipe, O_CLOEXEC) != 0 || pipe2(out_pipe, O_CLOEXEC) != 0) {
    throw std::runtime_error(std::string("pipe2 failed: ") + std::strerror(errno));
  }

  const pid_t pid = fork();
  if (pid < 0) {
    throw std::runtime_error(std::string("fork failed: ") + std::strerror(errno));
  }

  if (pid == 0) {
    // child: stdin <= in_pipe[0], stdout => out_pipe[1]
    if (::dup2(in_pipe[0], STDIN_FILENO) < 0 || ::dup2(out_pipe[1], STDOUT_FILENO) < 0) {
      _exit(126);
    }
    ::close(in_pipe[0]);
    ::close(in_pipe[1]);
    ::close(out_pipe[0]);
    ::close(out_pipe[1]);

    const std::string exe = config.executable.empty() ? SelfExe() : config.executable;
    const std::string shm_arg = "--shm-name=" + shm_name;
    execl(exe.c_str(), exe.c_str(), "--worker", shm_arg.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }

  ::close(in_pipe[0]);
  ::close(out_pipe[1]);
  slot.pid = pid;
  slot.to_worker = in_pipe[1];
  slot.from_worker = out_pipe[0];
  slot.jobs_since_spawn = 0;
  spdlog::info("spawned worker id={} pid={} shm={}", slot.id, static_cast<int>(pid), shm_name);
}

bool WorkerAlive(WorkerSlot& slot) {

  if (slot.pid <= 0) {
    return false;
  }

  int status = 0;
  const pid_t r = ::waitpid(slot.pid, &status, WNOHANG);

  if (r == 0) {
    return true;
  }

  slot.pid = -1;
  CloseFd(slot.to_worker);
  CloseFd(slot.from_worker);

  return false;
}

}  // namespace

PoolStats RunPool(const PoolConfig& config, const Sequence& sequence) {

  if (config.jobs < 1) {
    throw std::invalid_argument("jobs must be >= 1");
  }
  if (config.max_jobs < 1) {
    throw std::invalid_argument("max_jobs must be >= 1");
  }
  if (config.respawn_every_k < 1) {
    throw std::invalid_argument("respawn_every_k must be >= 1");
  }

  PoolConfig cfg = config;
  if (cfg.executable.empty()) {
    cfg.executable = SelfExe();
  }

  std::vector<uint8_t> global_cov(kCoverageBitmapSize, 0);
  PoolStats stats;

  std::vector<WorkerSlot> workers(static_cast<std::size_t>(cfg.jobs));
  for (int i = 0; i < cfg.jobs; ++i) {
    workers[static_cast<std::size_t>(i)].id = i;
    SpawnWorker(workers[static_cast<std::size_t>(i)], cfg);
  }

  auto shutdown = [&]() {
    for (auto& w : workers) {
      CloseFd(w.to_worker);  // EOF => worker exits loop
      if (w.pid > 0) {
        int status = 0;
        ::waitpid(w.pid, &status, 0);
        w.pid = -1;
      }
      CloseFd(w.from_worker);
    }
  };

  try {

    int next = 0;
    for (int job_i = 0; job_i < cfg.max_jobs; ++job_i) {
      auto& slot = workers[static_cast<std::size_t>(next % cfg.jobs)];
      ++next;

      if (!WorkerAlive(slot)) {
        spdlog::warn("worker id={} died; respawning", slot.id);
        SpawnWorker(slot, cfg);
        ++stats.respawns;
      }

      WorkerJob job;
      job.set_job_id(static_cast<uint64_t>(job_i));
      *job.mutable_sequence() = sequence;

      if (!WriteMessage(slot.to_worker, job)) {
        spdlog::error("failed to send job {} to worker {}", job_i, slot.id);
        SpawnWorker(slot, cfg);
        ++stats.respawns;
        ++stats.failed;
        continue;
      }

      WorkerResult result;
      if (!ReadMessage(slot.from_worker, &result)) {
        spdlog::error("failed to read result for job {} from worker {}", job_i, slot.id);
        SpawnWorker(slot, cfg);
        ++stats.respawns;
        ++stats.failed;
        continue;
      }

      if (result.status() == WorkerResult::OK) {
        ++stats.completed;
      } else {
        ++stats.failed;
        spdlog::warn("job {} failed: {}", job_i, result.error_message());
      }

      const uint32_t newly =
          CoverageShm::OrMerge(global_cov.data(), slot.shm.bitmap(), slot.shm.bitmap_size());
      stats.newly_merged_bits += newly;

      ++slot.jobs_since_spawn;

      if (slot.jobs_since_spawn >= cfg.respawn_every_k) {

        spdlog::info("respawn worker id={} after {} jobs", slot.id, slot.jobs_since_spawn);

        CloseFd(slot.to_worker);

        int status = 0;
        if (::waitpid(slot.pid, &status, 0) < 0) {
          KillWorker(slot);
        } 
        else {
          slot.pid = -1;
          CloseFd(slot.from_worker);
        }
        SpawnWorker(slot, cfg);
        ++stats.respawns;
      }
    }
  } catch (...) {
    shutdown();
    throw;
  }

  shutdown();

  stats.global_coverage_bits = 0;
  for (uint8_t b : global_cov) {
    stats.global_coverage_bits += b != 0 ? 1u : 0u;
  }
  
  return stats;
}

}  // namespace grpc_authz_fuzz::engine
