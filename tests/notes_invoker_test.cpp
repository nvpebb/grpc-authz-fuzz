#include <gtest/gtest.h>

#include <thread>

#include "notes/notes.pb.h"
#include "src/invoker/notes_invoker.h"

namespace {

grpc_authz_fuzz::Sequence GetSequence(const std::string& user = "alice") {
  notes::GetNoteRequest req;
  req.set_note_id("1");
  grpc_authz_fuzz::Sequence seq;
  seq.set_target_id("notes");
  auto* step = seq.add_steps();
  step->set_method("/notes.Notes/GetNote");
  step->set_request_type("notes.GetNoteRequest");
  step->set_principal_id(user);
  step->set_request(req.SerializeAsString());
  return seq;
}

}  // namespace

TEST(NotesInvoker, EchoAndPutNote) {
  grpc_authz_fuzz::invoker::NotesInvoker invoker;
  const auto result = invoker.Execute(grpc_authz_fuzz::invoker::MakeNotesSmokeSequence());
  ASSERT_TRUE(result.error.empty()) << result.error;
  ASSERT_EQ(result.steps.size(), 2u);
  EXPECT_EQ(result.steps[0].status_code, grpc::StatusCode::OK);
  EXPECT_EQ(result.steps[1].response_type, "notes.PutNoteResponse");
  notes::PutNoteResponse response;
  ASSERT_TRUE(response.ParseFromString(result.steps[1].response));
  EXPECT_EQ(response.note_id(), "1");
}

TEST(NotesInvoker, ResetClearsNotesAndRestoresIds) {
  grpc_authz_fuzz::invoker::NotesInvoker invoker;
  const auto smoke = grpc_authz_fuzz::invoker::MakeNotesSmokeSequence();
  ASSERT_TRUE(invoker.Execute(smoke).error.empty());
  auto result = invoker.Execute(GetSequence());
  ASSERT_EQ(result.steps.size(), 1u);
  EXPECT_EQ(result.steps[0].status_code, grpc::StatusCode::OK);

  std::string error;
  ASSERT_TRUE(invoker.Reset(&error)) << error;
  result = invoker.Execute(GetSequence());
  ASSERT_TRUE(result.error.empty());
  ASSERT_EQ(result.steps.size(), 1u);
  EXPECT_EQ(result.steps[0].status_code, grpc::StatusCode::NOT_FOUND);

  result = invoker.Execute(smoke);
  ASSERT_EQ(result.steps.size(), 2u);
  notes::PutNoteResponse response;
  ASSERT_TRUE(response.ParseFromString(result.steps[1].response));
  EXPECT_EQ(response.note_id(), "1");
}

TEST(NotesInvoker, ContinuesAfterNonOkRpcOutcome) {
  grpc_authz_fuzz::invoker::NotesInvoker invoker;
  auto seq = GetSequence();
  *seq.add_steps() = grpc_authz_fuzz::invoker::MakeNotesSmokeSequence().steps(0);
  const auto result = invoker.Execute(seq);
  ASSERT_TRUE(result.error.empty()) << result.error;
  ASSERT_EQ(result.steps.size(), 2u);
  EXPECT_EQ(result.steps[0].status_code, grpc::StatusCode::NOT_FOUND);
  EXPECT_EQ(result.steps[1].status_code, grpc::StatusCode::OK);
}

TEST(NotesInvoker, RejectsConflictingIdentity) {
  grpc_authz_fuzz::invoker::NotesInvoker invoker;
  auto seq = GetSequence("bob");
  (*seq.mutable_steps(0)->mutable_metadata())["x-user-id"] = "alice";
  const auto result = invoker.Execute(seq);
  EXPECT_TRUE(result.steps.empty());
  EXPECT_NE(result.error.find("conflicts"), std::string::npos);
}

TEST(NotesInvoker, ReportsCompletedStepsBeforeInvalidInput) {
  grpc_authz_fuzz::invoker::NotesInvoker invoker;
  auto seq = grpc_authz_fuzz::invoker::MakeNotesSmokeSequence();
  seq.mutable_steps(1)->set_request_type("notes.GetNoteRequest");
  const auto result = invoker.Execute(seq);
  EXPECT_EQ(result.steps.size(), 1u);
  EXPECT_NE(result.error.find("step 1"), std::string::npos);
}

TEST(NotesInvoker, RejectsUnsupportedTargetAndBindings) {
  grpc_authz_fuzz::invoker::NotesInvoker invoker;
  auto seq = GetSequence();
  seq.set_target_id("other");
  EXPECT_FALSE(invoker.Execute(seq).error.empty());
  seq.set_target_id("notes");
  seq.add_bindings();
  EXPECT_FALSE(invoker.Execute(seq).error.empty());
}

TEST(NotesInvoker, SyntheticTargetCurrentlyAllowsCrossUserRead) {
  grpc_authz_fuzz::invoker::NotesInvoker invoker;
  ASSERT_TRUE(invoker.Execute(grpc_authz_fuzz::invoker::MakeNotesSmokeSequence()).error.empty());
  const auto result = invoker.Execute(GetSequence("bob"));
  ASSERT_EQ(result.steps.size(), 1u);
  EXPECT_EQ(result.steps[0].status_code, grpc::StatusCode::OK);
  notes::GetNoteResponse response;
  ASSERT_TRUE(response.ParseFromString(result.steps[0].response));
  EXPECT_EQ(response.owner(), "alice");  // deliberately vulnerable synthetic target
}

namespace {

class SlowNotesService final : public notes::Notes::Service {
 public:
  grpc::Status Echo(grpc::ServerContext*, const notes::EchoRequest*,
                    notes::EchoResponse*) override {
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    return grpc::Status::OK;
  }
  grpc::Status Reset(grpc::ServerContext*, const notes::ResetRequest*,
                     notes::ResetResponse*) override {
    return {grpc::StatusCode::INTERNAL, "test reset failure"};
  }
};

}  // namespace

TEST(NotesInvoker, RpcDeadlineAndFailedResetAreReported) {
  SlowNotesService service;
  grpc::ServerBuilder builder;
  builder.RegisterService(&service);
  auto server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  grpc_authz_fuzz::invoker::NotesInvoker invoker(
      server->InProcessChannel(grpc::ChannelArguments()), std::chrono::milliseconds(30));
  auto seq = grpc_authz_fuzz::invoker::MakeNotesSmokeSequence();
  seq.mutable_steps()->RemoveLast();
  const auto result = invoker.Execute(seq);
  ASSERT_TRUE(result.error.empty()) << result.error;
  ASSERT_EQ(result.steps.size(), 1u);
  EXPECT_EQ(result.steps[0].status_code, grpc::StatusCode::DEADLINE_EXCEEDED);
  std::string error;
  EXPECT_FALSE(invoker.Reset(&error));
  EXPECT_FALSE(error.empty());
  server->Shutdown();
}

TEST(NotesInvoker, RejectsInvalidMetadataBeforeCallingGrpc) {
  grpc_authz_fuzz::invoker::NotesInvoker invoker;
  auto seq = GetSequence();
  (*seq.mutable_steps(0)->mutable_metadata())["X-Invalid"] = "value";
  EXPECT_FALSE(invoker.Execute(seq).error.empty());
  seq.mutable_steps(0)->clear_metadata();
  seq.mutable_steps(0)->set_principal_id("alice\nbob");
  EXPECT_FALSE(invoker.Execute(seq).error.empty());
}
