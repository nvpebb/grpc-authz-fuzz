#include <gtest/gtest.h>

#include <cstdlib>
#include <iostream>
#include <string>

#include "src/engine/pool.h"
#include "src/invoker/notes_invoker.h"

namespace {

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
  EXPECT_GE(stats.respawns, 2u);
  EXPECT_GE(stats.global_coverage_bits, 1u);
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
  EXPECT_GE(stats.respawns, 1u);
}
