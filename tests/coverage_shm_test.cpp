#include <gtest/gtest.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <stdexcept>

#include <string>
#include <vector>

#include "src/engine/coverage_shm.h"

TEST(CoverageShm, CreateOpenObserveMerge) {
  const std::string name = "/grpc_authz_fuzz_test_cov_" + std::to_string(::getpid());

  auto parent = grpc_authz_fuzz::engine::CoverageShm::Create(name);
  auto child = grpc_authz_fuzz::engine::CoverageShm::Open(name);

  child.ObserveMethod("/notes.Notes/Echo");
  child.ObserveMethod("/notes.Notes/PutNote");
  EXPECT_GE(child.CountOccupiedSlots(), 1u);

  std::vector<uint8_t> global(parent.bitmap_size(), 0);
  const uint32_t newly =
      grpc_authz_fuzz::engine::CoverageShm::OrMerge(global.data(), parent.bitmap(), parent.bitmap_size());
  EXPECT_EQ(newly, child.CountOccupiedSlots());
  EXPECT_EQ(0u, grpc_authz_fuzz::engine::CoverageShm::OrMerge(global.data(), parent.bitmap(),
                                                               parent.bitmap_size()));
}

TEST(CoverageShm, CollisionDoesNotUnlinkExistingMapping) {
  const std::string name = "/grpc_authz_fuzz_test_collision_" + std::to_string(::getpid());
  auto owner = grpc_authz_fuzz::engine::CoverageShm::Create(name);
  owner.ObserveMethod("/notes.Notes/Echo");
  EXPECT_THROW(grpc_authz_fuzz::engine::CoverageShm::Create(name), std::runtime_error);
  auto other = grpc_authz_fuzz::engine::CoverageShm::Open(name);
  EXPECT_EQ(other.CountOccupiedSlots(), 1u);
}

TEST(CoverageShm, RejectsTruncatedMapping) {
  const std::string name = "/grpc_authz_fuzz_test_truncated_" + std::to_string(::getpid());
  auto owner = grpc_authz_fuzz::engine::CoverageShm::Create(name);
  const int fd = shm_open(name.c_str(), O_RDWR, 0600);
  ASSERT_GE(fd, 0);
  ASSERT_EQ(ftruncate(fd, 1), 0);
  close(fd);
  EXPECT_THROW(grpc_authz_fuzz::engine::CoverageShm::Open(name), std::runtime_error);
}
