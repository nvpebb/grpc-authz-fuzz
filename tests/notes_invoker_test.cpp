#include <gtest/gtest.h>

#include "notes/notes.pb.h"
#include "src/invoker/notes_invoker.h"

TEST(NotesInvoker, EchoAndPutNote) {
  grpc_authz_fuzz::invoker::NotesInvoker invoker;
  const auto seq = grpc_authz_fuzz::invoker::MakeNotesSmokeSequence();
  std::string error;
  ASSERT_TRUE(invoker.Execute(seq, &error)) << error;
}

TEST(NotesInvoker, ResetClearsNotes) {
  grpc_authz_fuzz::invoker::NotesInvoker invoker;
  auto* server = invoker.server();
  ASSERT_NE(server, nullptr);

  std::string error;
  ASSERT_TRUE(invoker.Execute(grpc_authz_fuzz::invoker::MakeNotesSmokeSequence(), &error))
      << error;

  // PutNote in smoke seq creates note "1"
  notes::GetNoteRequest get_req;
  get_req.set_note_id("1");
  grpc_authz_fuzz::Sequence get_seq;
  get_seq.set_target_id("notes");
  auto* step = get_seq.add_steps();
  step->set_method("/notes.Notes/GetNote");
  step->set_request_type("notes.GetNoteRequest");
  step->set_principal_id("alice");
  step->mutable_metadata()->emplace("x-user-id", "alice");
  step->set_request(get_req.SerializeAsString());
  ASSERT_TRUE(invoker.Execute(get_seq, &error)) << error;

  invoker.Reset();
  EXPECT_FALSE(invoker.Execute(get_seq, &error));
}
