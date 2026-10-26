#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <CLI/CLI.hpp>
#include <google/protobuf/util/json_util.h>
#include <spdlog/spdlog.h>

#include "notes/notes.grpc.pb.h"
#include "src/engine/engine.h"
#include "src/engine/pool.h"
#include "src/engine/worker.h"
#include "src/invoker/notes_invoker.h"
#include "targets/notes/notes_inprocess.h"

namespace {

int RunNotesSmoke() {
  grpc_authz_fuzz::targets::notes::NotesInProcessServer server;
  auto stub = notes::Notes::NewStub(server.channel());

  notes::EchoRequest request;
  request.set_message("scaffold");
  notes::EchoResponse response;
  grpc::ClientContext context;
  const auto status = stub->Echo(&context, request, &response);
  if (!status.ok()) {
    spdlog::error("Echo failed: {}", status.error_message());
    return 1;
  }
  if (response.message() != "scaffold") {
    spdlog::error("unexpected echo response");
    return 1;
  }
  spdlog::info("notes smoke ok: {}", response.message());
  return 0;
}

bool LoadSequenceJson(const std::string& path, grpc_authz_fuzz::Sequence* out, std::string* error) {
  std::ifstream in(path);
  if (!in) {
    if (error) *error = "cannot open sequence file: " + path;
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  const auto status = google::protobuf::util::JsonStringToMessage(ss.str(), out);
  if (!status.ok()) {
    if (error) *error = std::string(status.message());
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"grpc-authz-fuzz, coverage-guided gRPC AuthZ fuzzer"};
  app.set_version_flag("-V,--version", grpc_authz_fuzz::engine::EngineInfo::Version());

  int jobs = 1;
  int max_jobs = 1;
  int respawn_every_k = 100;
  std::string config_path;
  std::string sequence_path;
  std::string oracles = "none";
  std::string shm_name;
  bool worker_mode = false;
  bool notes_smoke = false;

  app.add_option("-j,--jobs", jobs, "Worker pool size")->check(CLI::PositiveNumber);
  app.add_option("--max-jobs", max_jobs, "Stop after N jobs (scaffold finite run)")
      ->check(CLI::PositiveNumber);
  app.add_option("--respawn-every-k", respawn_every_k, "Respawn each worker after K jobs")
      ->check(CLI::PositiveNumber);
  app.add_option("-c,--config", config_path, "Path to fuzz.yaml");
  app.add_option("--sequence", sequence_path, "Path to Sequence JSON (default: built-in Notes smoke)");
  app.add_option("--oracles", oracles, "Comma-separated oracles (none|bola|divergence)");
  app.add_flag("--worker", worker_mode, "Run as worker process");
  app.add_option("--shm-name", shm_name, "Coverage SHM name (worker mode)");
  app.add_flag("--notes-smoke", notes_smoke, "Run in-process Notes smoke and exit");

  auto* run = app.add_subcommand("run", "Start a fuzz campaign");
  run->fallthrough();

  CLI11_PARSE(app, argc, argv);

  if (notes_smoke) {
    return RunNotesSmoke();
  }

  if (worker_mode) {
    if (shm_name.empty()) {
      spdlog::error("--worker requires --shm-name");
      return 1;
    }
    grpc_authz_fuzz::invoker::NotesInvoker invoker;
    return grpc_authz_fuzz::engine::RunWorkerLoop(&invoker, shm_name);
  }

  if (run->parsed()) {
    grpc_authz_fuzz::Sequence sequence;
    if (!sequence_path.empty()) {
      std::string error;
      if (!LoadSequenceJson(sequence_path, &sequence, &error)) {
        spdlog::error("{}", error);
        return 1;
      }
    } else {
      sequence = grpc_authz_fuzz::invoker::MakeNotesSmokeSequence();
    }

    grpc_authz_fuzz::engine::PoolConfig pool_cfg;
    pool_cfg.jobs = jobs;
    pool_cfg.max_jobs = max_jobs;
    pool_cfg.respawn_every_k = respawn_every_k;

    try {
      const auto stats = grpc_authz_fuzz::engine::RunPool(pool_cfg, sequence);
      std::cout << grpc_authz_fuzz::engine::EngineInfo::Name() << ' '
                << grpc_authz_fuzz::engine::EngineInfo::Version() << " run: completed="
                << stats.completed << " failed=" << stats.failed << " respawns=" << stats.respawns
                << " coverage_bits=" << stats.global_coverage_bits
                << " newly_merged=" << stats.newly_merged_bits;
      if (!config_path.empty()) {
        std::cout << " config=" << config_path;
      }
      std::cout << " oracles=" << oracles << '\n';
      return stats.failed == 0 ? 0 : 1;
    } catch (const std::exception& ex) {
      spdlog::error("pool failed: {}", ex.what());
      return 1;
    }
  }

  std::cout << app.help();
  return 0;
}
