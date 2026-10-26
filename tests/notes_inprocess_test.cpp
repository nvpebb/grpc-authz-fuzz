#include <gtest/gtest.h>

#include <grpcpp/grpcpp.h>

#include "notes/notes.grpc.pb.h"
#include "targets/notes/notes_inprocess.h"

namespace {

void SetUser(grpc::ClientContext* ctx, const std::string& user) {
  ctx->AddMetadata("x-user-id", user);
}

}  // namespace

TEST(NotesInProcess, EchoAndNotesWithReset) {
  grpc_authz_fuzz::targets::notes::NotesInProcessServer server;
  auto stub = notes::Notes::NewStub(server.channel());

  {
    notes::EchoRequest req;
    req.set_message("ping");
    notes::EchoResponse resp;
    grpc::ClientContext ctx;
    ASSERT_TRUE(stub->Echo(&ctx, req, &resp).ok());
    EXPECT_EQ(resp.message(), "ping");
  }

  std::string note_id;
  {
    notes::PutNoteRequest req;
    req.set_title("scaffold");
    notes::PutNoteResponse resp;
    grpc::ClientContext ctx;
    SetUser(&ctx, "alice");
    ASSERT_TRUE(stub->PutNote(&ctx, req, &resp).ok());
    note_id = resp.note_id();
    EXPECT_FALSE(note_id.empty());
  }

  {
    notes::GetNoteRequest req;
    req.set_note_id(note_id);
    notes::GetNoteResponse resp;
    grpc::ClientContext ctx;
    SetUser(&ctx, "alice");
    ASSERT_TRUE(stub->GetNote(&ctx, req, &resp).ok());
    EXPECT_EQ(resp.title(), "scaffold");
    EXPECT_EQ(resp.owner(), "alice");
  }

  {
    notes::ResetRequest req;
    notes::ResetResponse resp;
    grpc::ClientContext ctx;
    ASSERT_TRUE(stub->Reset(&ctx, req, &resp).ok());
  }

  {
    notes::GetNoteRequest req;
    req.set_note_id(note_id);
    notes::GetNoteResponse resp;
    grpc::ClientContext ctx;
    SetUser(&ctx, "alice");
    const auto status = stub->GetNote(&ctx, req, &resp);
    EXPECT_EQ(status.error_code(), grpc::StatusCode::NOT_FOUND);
  }
}
