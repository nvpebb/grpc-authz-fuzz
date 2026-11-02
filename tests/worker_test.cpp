#include <gtest/gtest.h>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <string>

#include "notes/notes.pb.h"
#include "src/engine/coverage_shm.h"
#include "src/engine/ipc.h"
#include "src/engine/worker.h"
#include "src/invoker/notes_invoker.h"
#include "worker.pb.h"

namespace {

class FailedResetInvoker final : public grpc_authz_fuzz::invoker::RpcInvoker {
 public:
  bool Reset(std::string* error) override {
    *error = "test reset failure";
    return false;
  }
  grpc_authz_fuzz::invoker::ExecutionResult Execute(const grpc_authz_fuzz::Sequence&) override {
    return {{}, "Execute must not run after failed reset"};
  }
};

class WorkerTest : public ::testing::Test {
 protected:
  void Start(bool failed_reset = false) {
    name_ = "/grpc_authz_fuzz_worker_test_" + std::to_string(getpid());
    shm_ = grpc_authz_fuzz::engine::CoverageShm::Create(name_);
    int input[2];
    int output[2];
    ASSERT_EQ(pipe2(input, O_CLOEXEC), 0);
    ASSERT_EQ(pipe2(output, O_CLOEXEC), 0);
    pid_ = fork();
    ASSERT_GE(pid_, 0);
    if (pid_ == 0) {
      dup2(input[0], STDIN_FILENO);
      dup2(output[1], STDOUT_FILENO);
      execl("/proc/self/exe", "worker_test", "--worker", name_.c_str(),
            failed_reset ? "fail-reset" : "notes", static_cast<char*>(nullptr));
      _exit(127);
    }
    close(input[0]);
    close(output[1]);
    input_ = input[1];
    output_ = output[0];
    fcntl(input_, F_SETFL, O_NONBLOCK);
    fcntl(output_, F_SETFL, O_NONBLOCK);
  }

  void TearDown() override {
    if (input_ >= 0) close(input_);
    if (output_ >= 0) close(output_);
    if (pid_ > 0) {
      kill(pid_, SIGKILL);
      while (waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {}
    }
  }

  grpc_authz_fuzz::WorkerResult Run(uint64_t id, const grpc_authz_fuzz::Sequence& seq) {
    grpc_authz_fuzz::WorkerJob job;
    job.set_job_id(id);
    *job.mutable_sequence() = seq;
    EXPECT_TRUE(grpc_authz_fuzz::engine::WriteMessage(input_, job, 2000));
    grpc_authz_fuzz::WorkerResult result;
    EXPECT_TRUE(grpc_authz_fuzz::engine::ReadMessage(output_, &result, 2000));
    EXPECT_EQ(result.job_id(), id);
    return result;
  }

  std::string name_;
  grpc_authz_fuzz::engine::CoverageShm shm_;
  pid_t pid_ = -1;
  int input_ = -1;
  int output_ = -1;
};

TEST_F(WorkerTest, ResetsBetweenJobsInSameProcess) {
  Start();
  const auto seq = grpc_authz_fuzz::invoker::MakeNotesSmokeSequence();
  for (uint64_t id = 0; id < 2; ++id) {
    const auto result = Run(id, seq);
    ASSERT_EQ(result.status(), grpc_authz_fuzz::WorkerResult::OK);
    ASSERT_EQ(result.outcomes_size(), 2);
    notes::PutNoteResponse response;
    ASSERT_TRUE(response.ParseFromString(result.outcomes(1).response()));
    EXPECT_EQ(response.note_id(), "1");
    EXPECT_EQ(result.steps_executed(), 2u);
  }
}

TEST_F(WorkerTest, CoverageOnlyIncludesExecutedSteps) {
  Start();
  auto seq = grpc_authz_fuzz::invoker::MakeNotesSmokeSequence();
  seq.mutable_steps(1)->set_request_type("wrong.Type");
  const auto result = Run(0, seq);
  EXPECT_EQ(result.status(), grpc_authz_fuzz::WorkerResult::TARGET_ERROR);
  EXPECT_EQ(result.steps_executed(), 1u);
  EXPECT_EQ(result.outcomes_size(), 1);
  EXPECT_EQ(result.coverage_slots_set(), 1u);
  // A rejected job must also clear the previous job's coverage.
  seq.set_target_id("other");
  EXPECT_EQ(Run(1, seq).coverage_slots_set(), 0u);
}

TEST_F(WorkerTest, FailedResetIsInfrastructureError) {
  Start(true);
  const auto result = Run(0, grpc_authz_fuzz::invoker::MakeNotesSmokeSequence());
  EXPECT_EQ(result.status(), grpc_authz_fuzz::WorkerResult::INTERNAL_ERROR);
  EXPECT_EQ(result.steps_executed(), 0u);
  EXPECT_EQ(result.outcomes_size(), 0);
  EXPECT_EQ(result.error_message(), "test reset failure");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 4 && std::string(argv[1]) == "--worker") {
    if (std::string(argv[3]) == "fail-reset") {
      FailedResetInvoker invoker;
      return grpc_authz_fuzz::engine::RunWorkerLoop(&invoker, argv[2]);
    }
    grpc_authz_fuzz::invoker::NotesInvoker invoker;
    return grpc_authz_fuzz::engine::RunWorkerLoop(&invoker, argv[2]);
  }
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
