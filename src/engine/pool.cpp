#include "src/engine/pool.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
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

using Clock = std::chrono::steady_clock;

void CloseFd(int& fd) {
  if (fd >= 0) {
    ::close(fd);
    fd = -1;
  }
}

struct Pipe {
  int ends[2] = {-1, -1};

  Pipe() {
    if (pipe2(ends, O_CLOEXEC) != 0) {
      throw std::runtime_error(std::string("pipe2 failed: ") + std::strerror(errno));
    }
  }
  ~Pipe() {
    CloseFd(ends[0]);
    CloseFd(ends[1]);
  }
  Pipe(const Pipe&) = delete;
  Pipe& operator=(const Pipe&) = delete;
};

struct WorkerSlot {
  int id = 0;
  int generation = 0;
  pid_t pid = -1;
  int to_worker = -1;
  int from_worker = -1;
  CoverageShm shm;
  MessageReader reader;
  MessageWriter writer;
  int jobs_since_spawn = 0;
  bool busy = false;
  bool sending = false;
  uint64_t job_id = 0;
  Clock::time_point deadline;

  ~WorkerSlot() { Stop(); }
  WorkerSlot() = default;
  WorkerSlot(const WorkerSlot&) = delete;
  WorkerSlot& operator=(const WorkerSlot&) = delete;

  void Stop() {
    CloseFd(to_worker);
    CloseFd(from_worker);
    if (pid > 0) {
      // The worker may be stuck inside the target; cleanup must not depend on EOF.
      ::kill(pid, SIGKILL);
      while (::waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
      pid = -1;
    }
    busy = false;
    reader.Reset();
    writer.Reset();
    shm = CoverageShm();
  }
};

std::string SelfExe() {
  std::array<char, 4096> buf{};
  const ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size());
  if (n < 0 || static_cast<std::size_t>(n) == buf.size()) {
    throw std::runtime_error("cannot resolve /proc/self/exe");
  }
  return std::string(buf.data(), static_cast<std::size_t>(n));
}

void SetNonblocking(int fd) {
  const int flags = fcntl(fd, F_GETFL);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    throw std::runtime_error(std::string("fcntl failed: ") + std::strerror(errno));
  }
}

void SpawnWorker(WorkerSlot& slot, const PoolConfig& config) {
  slot.Stop();
  ++slot.generation;
  const std::string shm_name = config.shm_prefix + "_" + std::to_string(getpid()) + "_" +
                               std::to_string(slot.id) + "_" + std::to_string(slot.generation);
  slot.shm = CoverageShm::Create(shm_name);
  Pipe input;
  Pipe output;
  // Build arguments before fork; the child only redirects descriptors and execs.
  const std::string shm_arg = "--shm-name=" + shm_name;
  const pid_t pid = fork();
  if (pid < 0) {
    throw std::runtime_error(std::string("fork failed: ") + std::strerror(errno));
  }
  if (pid == 0) {
    if (::dup2(input.ends[0], STDIN_FILENO) < 0 ||
        ::dup2(output.ends[1], STDOUT_FILENO) < 0) {
      _exit(126);
    }
    // Keep stdin/stdout even if the caller originally had either one closed.
    for (int fd : {input.ends[0], input.ends[1], output.ends[0], output.ends[1]}) {
      if (fd != STDIN_FILENO && fd != STDOUT_FILENO) {
        ::close(fd);
      }
    }
    fcntl(STDIN_FILENO, F_SETFD, 0);
    fcntl(STDOUT_FILENO, F_SETFD, 0);
    execl(config.executable.c_str(), config.executable.c_str(), "--worker", shm_arg.c_str(),
          static_cast<char*>(nullptr));
    _exit(127);
  }
  slot.pid = pid;
  slot.to_worker = std::exchange(input.ends[1], -1);
  slot.from_worker = std::exchange(output.ends[0], -1);
  SetNonblocking(slot.to_worker);
  SetNonblocking(slot.from_worker);
  slot.jobs_since_spawn = 0;
  spdlog::debug("spawned worker id={} pid={}", slot.id, static_cast<int>(pid));
}

int Remaining(Clock::time_point deadline) {
  const auto ms = std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now()).count();
  return static_cast<int>(std::max<int64_t>(0, ms));
}

}  // namespace

PoolStats RunPool(const PoolConfig& config, const Sequence& sequence) {
  if (config.jobs < 1 || config.max_jobs < 1 || config.respawn_every_k < 1 ||
      config.job_timeout_ms < 1) {
    throw std::invalid_argument("jobs, max_jobs, respawn_every_k and job_timeout_ms must be >= 1");
  }
  PoolConfig cfg = config;
  if (cfg.executable.empty()) {
    cfg.executable = SelfExe();
  }
  std::vector<uint8_t> global_cov(kCoverageBitmapSize, 0);
  PoolStats stats;
  std::vector<WorkerSlot> workers(static_cast<std::size_t>(std::min(cfg.jobs, cfg.max_jobs)));
  for (std::size_t i = 0; i < workers.size(); ++i) {
    workers[i].id = static_cast<int>(i);
  }

  int submitted = 0;
  while (stats.completed + stats.failed < static_cast<uint64_t>(cfg.max_jobs)) {
    for (auto& slot : workers) {
      if (slot.busy || submitted == cfg.max_jobs) {
        continue;
      }
      if (slot.pid <= 0) {
        const bool respawn = slot.generation > 0;
        SpawnWorker(slot, cfg);
        stats.respawns += respawn ? 1u : 0u;
      }
      slot.job_id = static_cast<uint64_t>(submitted++);
      slot.deadline = Clock::now() + std::chrono::milliseconds(cfg.job_timeout_ms);
      WorkerJob job;
      job.set_job_id(slot.job_id);
      *job.mutable_sequence() = sequence;
      if (!slot.writer.Prepare(job)) {
        ++stats.failed;
        slot.Stop();
        continue;
      }
      slot.busy = true;
      slot.sending = true;
    }

    std::vector<pollfd> fds;
    int timeout = cfg.job_timeout_ms;
    for (const auto& slot : workers) {
      const int fd = slot.sending ? slot.to_worker : slot.from_worker;
      fds.push_back({slot.busy ? fd : -1, static_cast<short>(slot.sending ? POLLOUT : POLLIN), 0});
      if (slot.busy) {
        timeout = std::min(timeout, Remaining(slot.deadline));
      }
    }
    if (stats.completed + stats.failed == static_cast<uint64_t>(cfg.max_jobs)) {
      break;
    }
    if (std::none_of(workers.begin(), workers.end(), [](const auto& slot) { return slot.busy; })) {
      continue;
    }
    const int ready = poll(fds.data(), fds.size(), timeout);
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::runtime_error(std::string("poll failed: ") + std::strerror(errno));
    }
    for (std::size_t i = 0; i < workers.size(); ++i) {
      auto& slot = workers[i];
      if (!slot.busy) {
        continue;
      }
      if (Remaining(slot.deadline) == 0) {
        spdlog::warn("job {} timed out", slot.job_id);
        ++stats.failed;
        slot.Stop();
        continue;
      }
      if (fds[i].revents == 0) {
        continue;
      }
      if (slot.sending) {
        const auto written = slot.writer.Write(slot.to_worker);
        if (written == IoStatus::ERROR) {
          ++stats.failed;
          slot.Stop();
        } else if (written == IoStatus::COMPLETE) {
          slot.sending = false;
        }
        continue;
      }
      WorkerResult result;
      const auto read = slot.reader.Read(slot.from_worker, &result);
      if (read == IoStatus::PENDING) {
        continue;
      }
      if (read == IoStatus::ERROR || result.job_id() != slot.job_id ||
          (result.status() != WorkerResult::OK && result.status() != WorkerResult::TARGET_ERROR &&
           result.status() != WorkerResult::INTERNAL_ERROR)) {
        spdlog::warn("invalid or missing result for job {}", slot.job_id);
        ++stats.failed;
        slot.Stop();
        continue;
      }
      if (result.status() == WorkerResult::OK) {
        ++stats.completed;
      } else {
        ++stats.failed;
        spdlog::warn("job {} failed: {}", slot.job_id, result.error_message());
      }
      stats.newly_merged_slots +=
          CoverageShm::OrMerge(global_cov.data(), slot.shm.bitmap(), slot.shm.bitmap_size());
      slot.busy = false;
      ++slot.jobs_since_spawn;
      if (result.status() == WorkerResult::INTERNAL_ERROR ||
          slot.jobs_since_spawn >= cfg.respawn_every_k) {
        slot.Stop();  // respawn only when there is another job to submit
      }
    }
  }
  for (uint8_t b : global_cov) {
    stats.global_coverage_slots += b != 0 ? 1u : 0u;
  }
  return stats;
}

}  // namespace grpc_authz_fuzz::engine
