#include <gtest/gtest.h>

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include "src/engine/ipc.h"
#include "worker.pb.h"

TEST(Ipc, BrokenPipeReturnsFailureAndRestoresSignalMask) {
  int fds[2];
  ASSERT_EQ(pipe2(fds, O_CLOEXEC), 0);
  close(fds[0]);
  sigset_t before;
  sigset_t after;
  pthread_sigmask(SIG_SETMASK, nullptr, &before);
  grpc_authz_fuzz::WorkerJob job;
  EXPECT_FALSE(grpc_authz_fuzz::engine::WriteMessage(fds[1], job));
  pthread_sigmask(SIG_SETMASK, nullptr, &after);
  EXPECT_EQ(sigismember(&before, SIGPIPE), sigismember(&after, SIGPIPE));
  close(fds[1]);
}

TEST(Ipc, PartialHeaderDoesNotBlockIncrementalReader) {
  int fds[2];
  ASSERT_EQ(pipe2(fds, O_CLOEXEC | O_NONBLOCK), 0);
  const char partial = 10;
  ASSERT_EQ(write(fds[1], &partial, 1), 1);
  grpc_authz_fuzz::engine::MessageReader reader;
  grpc_authz_fuzz::WorkerResult result;
  EXPECT_EQ(reader.Read(fds[0], &result), grpc_authz_fuzz::engine::IoStatus::PENDING);
  EXPECT_FALSE(grpc_authz_fuzz::engine::ReadMessage(fds[0], &result, 20));
  close(fds[0]);
  close(fds[1]);
}

TEST(Ipc, IncrementalReaderAcceptsCompleteFrame) {
  int fds[2];
  ASSERT_EQ(pipe2(fds, O_CLOEXEC | O_NONBLOCK), 0);
  grpc_authz_fuzz::WorkerResult sent;
  sent.set_job_id(42);
  sent.set_status(grpc_authz_fuzz::WorkerResult::OK);
  ASSERT_TRUE(grpc_authz_fuzz::engine::WriteMessage(fds[1], sent, 100));
  grpc_authz_fuzz::engine::MessageReader reader;
  grpc_authz_fuzz::WorkerResult result;
  EXPECT_EQ(reader.Read(fds[0], &result), grpc_authz_fuzz::engine::IoStatus::COMPLETE);
  EXPECT_EQ(result.job_id(), 42u);
  close(fds[0]);
  close(fds[1]);
}
