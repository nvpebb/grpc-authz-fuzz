#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sequence.pb.h"

namespace grpc_authz_fuzz::engine {

struct PoolConfig {
  std::string executable;     // absolute path to this binary (/proc/self/exe)
  int jobs = 1;               // -j
  int max_jobs = 1;           // finite campaign for scaffold
  int respawn_every_k = 100;  // recycle worker after K successful handoffs
  std::string shm_prefix = "/grpc_authz_fuzz_cov";
};

struct PoolStats {
  uint64_t completed = 0;
  uint64_t failed = 0;
  uint64_t respawns = 0;
  uint32_t global_coverage_bits = 0;
  uint32_t newly_merged_bits = 0;
};

// spawns --worker children, feeds the same sequence max_jobs times,
PoolStats RunPool(const PoolConfig& config, const Sequence& sequence);

}  // namespace grpc_authz_fuzz::engine
