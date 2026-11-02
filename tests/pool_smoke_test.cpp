#include <gtest/gtest.h>

#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#include <iostream>
#include <string>

#include "src/engine/pool.h"
#include "src/engine/ipc.h"
#include "worker.pb.h"
#include "src/invoker/notes_invoker.h"

namespace {

int RunTestWorker() {
  using namespace grpc_authz_fuzz;
  WorkerJob job;
  while (engine::ReadMessage(STDIN_FILENO, &job)) {
    const auto& mode = job.sequence().target_id();
    if (mode == "die" || (mode == "recover" && job.job_id() == 0)) {
      return 7;
    }
    if (mode == "hang" || mode == "partial") {
      if (mode == "partial") {
        const char header[2] = {10, 0};
        engine::WriteAll(STDOUT_FILENO, header, sizeof(header));
      }
      for (;;) {
        pause();
      }
    }
    WorkerResult result;
    result.set_job_id(mode == "wrong-id" ? job.job_id() + 1 : job.job_id());
    result.set_status(WorkerResult::OK);
    if (mode == "parallel") {
      const std::filesystem::path dir(job.sequence().steps(0).request());
      std::ofstream(dir / std::to_string(job.job_id())).put('1');
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
      while (!std::filesystem::exists(dir / std::to_string(1 - job.job_id()))) {
        if (std::chrono::steady_clock::now() >= deadline) {
          result.set_status(WorkerResult::INTERNAL_ERROR);
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    }
    if (!engine::WriteMessage(STDOUT_FILENO, result)) {
      return 1;
    }
  }
  return 0;
}

std::string BinFromEnvOrArg(int argc, char** argv) {
  if (const char* env = std::getenv("GRPC_AUTHZ_FUZZ_BIN")) {
    return env;
  }
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == "--bin") {
      return argv[i + 1];
    }
  }
  return {};
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--worker") {
    return RunTestWorker();
  }
  ::testing::InitGoogleTest(&argc, argv);
  const std::string bin = BinFromEnvOrArg(argc, argv);
  if (bin.empty()) {
    std::cerr << "pool_smoke_test requires GRPC_AUTHZ_FUZZ_BIN or --bin <path>\n";
    return 1;
  }
  // stash in env for TEST bodies (InitGoogleTest may consume argv)
  ::setenv("GRPC_AUTHZ_FUZZ_BIN", bin.c_str(), 1);
  return RUN_ALL_TESTS();
}

TEST(PoolSmoke, RunsFixedSequenceWithRespawn) {
  const char* bin = std::getenv("GRPC_AUTHZ_FUZZ_BIN");
  ASSERT_NE(bin, nullptr);

  grpc_authz_fuzz::engine::PoolConfig cfg;
  cfg.executable = bin;
  cfg.jobs = 1;
  cfg.max_jobs = 3;
  cfg.respawn_every_k = 1;  // force respawn after every job
  cfg.shm_prefix = "/grpc_authz_fuzz_pool_test";

  const auto seq = grpc_authz_fuzz::invoker::MakeNotesSmokeSequence();
  const auto stats = grpc_authz_fuzz::engine::RunPool(cfg, seq);

  EXPECT_EQ(stats.completed, 3u);
  EXPECT_EQ(stats.failed, 0u);
  EXPECT_EQ(stats.respawns, 2u);
  EXPECT_GE(stats.global_coverage_slots, 1u);
}

TEST(PoolSmoke, TwoWorkersRespawnDoesNotHang) {
  const char* bin = std::getenv("GRPC_AUTHZ_FUZZ_BIN");
  ASSERT_NE(bin, nullptr);

  grpc_authz_fuzz::engine::PoolConfig cfg;
  cfg.executable = bin;
  cfg.jobs = 2;
  cfg.max_jobs = 4;
  cfg.respawn_every_k = 2;
  cfg.shm_prefix = "/grpc_authz_fuzz_pool_j2";

  const auto seq = grpc_authz_fuzz::invoker::MakeNotesSmokeSequence();
  const auto stats = grpc_authz_fuzz::engine::RunPool(cfg, seq);

  EXPECT_EQ(stats.completed, 4u);
  EXPECT_EQ(stats.failed, 0u);
  EXPECT_LE(stats.respawns, 1u);
}

TEST(PoolFailures, RecoversAfterWorkerDeath) {
  grpc_authz_fuzz::engine::PoolConfig cfg;
  cfg.executable = "/proc/self/exe";
  cfg.max_jobs = 4;
  grpc_authz_fuzz::Sequence seq;
  seq.set_target_id("recover");
  const auto stats = grpc_authz_fuzz::engine::RunPool(cfg, seq);
  EXPECT_EQ(stats.failed, 1u);
  EXPECT_EQ(stats.completed, 3u);
  EXPECT_EQ(stats.respawns, 1u);
  EXPECT_EQ(waitpid(-1, nullptr, WNOHANG), -1);
  EXPECT_EQ(errno, ECHILD);
}

TEST(PoolFailures, BoundsHungAndPartialResults) {
  for (const auto* mode : {"hang", "partial", "wrong-id", "die"}) {
    grpc_authz_fuzz::engine::PoolConfig cfg;
    cfg.executable = "/proc/self/exe";
    cfg.job_timeout_ms = 200;
    cfg.max_jobs = 2;
    grpc_authz_fuzz::Sequence seq;
    seq.set_target_id(mode);
    const auto stats = grpc_authz_fuzz::engine::RunPool(cfg, seq);
    EXPECT_EQ(stats.failed, 2u) << mode;
    EXPECT_EQ(stats.completed, 0u) << mode;
    EXPECT_EQ(stats.respawns, 1u) << mode;
    EXPECT_EQ(waitpid(-1, nullptr, WNOHANG), -1);
    EXPECT_EQ(errno, ECHILD);
  }
}

TEST(PoolFailures, InvalidExecutableDoesNotKillParent) {
  grpc_authz_fuzz::engine::PoolConfig cfg;
  cfg.executable = "/no/such/grpc-authz-fuzz";
  cfg.max_jobs = 3;
  const auto stats = grpc_authz_fuzz::engine::RunPool(cfg, grpc_authz_fuzz::Sequence());
  EXPECT_EQ(stats.failed, 3u);
  EXPECT_EQ(stats.completed, 0u);
}

TEST(PoolSmoke, DispatchesTwoJobsConcurrently) {
  const auto dir = std::filesystem::temp_directory_path() /
                   ("grpc_authz_fuzz_parallel_" + std::to_string(getpid()));
  ASSERT_TRUE(std::filesystem::create_directory(dir));
  grpc_authz_fuzz::Sequence seq;
  seq.set_target_id("parallel");
  seq.add_steps()->set_request(dir.string());
  grpc_authz_fuzz::engine::PoolConfig cfg;
  cfg.executable = "/proc/self/exe";
  cfg.jobs = 2;
  cfg.max_jobs = 2;
  const auto stats = grpc_authz_fuzz::engine::RunPool(cfg, seq);
  EXPECT_EQ(stats.completed, 2u);
  EXPECT_EQ(stats.failed, 0u);
  std::filesystem::remove_all(dir);
}

TEST(PoolSmoke, DoesNotRespawnAfterLastJob) {
  grpc_authz_fuzz::engine::PoolConfig cfg;
  cfg.executable = "/proc/self/exe";
  cfg.max_jobs = 2;
  cfg.respawn_every_k = 2;
  grpc_authz_fuzz::Sequence seq;
  seq.set_target_id("ok");
  const auto stats = grpc_authz_fuzz::engine::RunPool(cfg, seq);
  EXPECT_EQ(stats.completed, 2u);
  EXPECT_EQ(stats.respawns, 0u);
}
