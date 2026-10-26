#include <gtest/gtest.h>

#include <unistd.h>

#include <string>
#include <vector>

#include "src/engine/coverage_shm.h"

TEST(CoverageShm, CreateOpenObserveMerge) {
  const std::string name = "/grpc_authz_fuzz_test_cov_" + std::to_string(::getpid());

  auto parent = grpc_authz_fuzz::engine::CoverageShm::Create(name);
  auto child = grpc_authz_fuzz::engine::CoverageShm::Open(name);

  child.ObserveMethod("/notes.Notes/Echo");
  child.ObserveMethod("/notes.Notes/PutNote");
  EXPECT_GE(child.CountSetBits(), 1u);

  std::vector<uint8_t> global(parent.bitmap_size(), 0);
  const uint32_t newly =
      grpc_authz_fuzz::engine::CoverageShm::OrMerge(global.data(), parent.bitmap(), parent.bitmap_size());
  EXPECT_EQ(newly, child.CountSetBits());
  EXPECT_EQ(0u, grpc_authz_fuzz::engine::CoverageShm::OrMerge(global.data(), parent.bitmap(),
                                                               parent.bitmap_size()));
}
