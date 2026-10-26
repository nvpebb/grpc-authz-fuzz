#include "src/invoker/notes_invoker.h"

#include <utility>

#include "notes/notes.grpc.pb.h"

namespace grpc_authz_fuzz::invoker {
namespace {

void ApplyMetadata(grpc::ClientContext* ctx, const Step& step) {
  for (const auto& [key, value] : step.metadata()) {
    ctx->AddMetadata(key, value);
  }
  if (!step.principal_id().empty() && step.metadata().count("x-user-id") == 0) {
    ctx->AddMetadata("x-user-id", step.principal_id());
  }
}

bool MethodIs(const std::string& method, const char* suffix) {
  return method == suffix || method == std::string("/") + suffix;
}

}  // namespace

NotesInvoker::NotesInvoker() : server_(std::make_unique<targets::notes::NotesInProcessServer>()) {
  stub_ = notes::Notes::NewStub(server_->channel());
}

NotesInvoker::NotesInvoker(std::shared_ptr<grpc::Channel> channel)
    : stub_(notes::Notes::NewStub(std::move(channel))) {}

void NotesInvoker::Reset() {
  if (server_) {
    server_->Reset();
  } else {
    notes::ResetRequest req;
    notes::ResetResponse resp;
    grpc::ClientContext ctx;
    stub_->Reset(&ctx, req, &resp);
  }
}

bool NotesInvoker::Execute(const Sequence& sequence, std::string* error) {
  for (int i = 0; i < sequence.steps_size(); ++i) {
    if (!ExecuteStep(sequence.steps(i), error)) {
      if (error != nullptr && error->empty()) {
        *error = "step " + std::to_string(i) + " failed";
      }
      return false;
    }
  }
  return true;
}

bool NotesInvoker::ExecuteStep(const Step& step, std::string* error) {
  grpc::ClientContext ctx;
  ApplyMetadata(&ctx, step);

  if (MethodIs(step.method(), "notes.Notes/Echo")) {
    notes::EchoRequest req;
    if (!req.ParseFromString(step.request())) {
      if (error) *error = "failed to parse EchoRequest";
      return false;
    }
    notes::EchoResponse resp;
    const auto status = stub_->Echo(&ctx, req, &resp);
    if (!status.ok()) {
      if (error) *error = status.error_message();
      return false;
    }
    return true;
  }

  if (MethodIs(step.method(), "notes.Notes/PutNote")) {
    notes::PutNoteRequest req;
    if (!req.ParseFromString(step.request())) {
      if (error) *error = "failed to parse PutNoteRequest";
      return false;
    }
    notes::PutNoteResponse resp;
    const auto status = stub_->PutNote(&ctx, req, &resp);
    if (!status.ok()) {
      if (error) *error = status.error_message();
      return false;
    }
    return true;
  }

  if (MethodIs(step.method(), "notes.Notes/GetNote")) {
    notes::GetNoteRequest req;
    if (!req.ParseFromString(step.request())) {
      if (error) *error = "failed to parse GetNoteRequest";
      return false;
    }
    notes::GetNoteResponse resp;
    const auto status = stub_->GetNote(&ctx, req, &resp);
    if (!status.ok()) {
      if (error) *error = status.error_message();
      return false;
    }
    return true;
  }

  if (MethodIs(step.method(), "notes.Notes/Reset")) {
    notes::ResetRequest req;
    notes::ResetResponse resp;
    const auto status = stub_->Reset(&ctx, req, &resp);
    if (!status.ok()) {
      if (error) *error = status.error_message();
      return false;
    }
    return true;
  }

  if (error) *error = "unknown method: " + step.method();
  return false;
}

Sequence MakeNotesSmokeSequence() {
  Sequence seq;
  seq.set_target_id("notes");

  {
    notes::EchoRequest req;
    req.set_message("scaffold");
    auto* step = seq.add_steps();
    step->set_method("/notes.Notes/Echo");
    step->set_request_type("notes.EchoRequest");
    step->set_principal_id("alice");
    step->mutable_metadata()->emplace("x-user-id", "alice");
    step->set_request(req.SerializeAsString());
  }

  {
    notes::PutNoteRequest req;
    req.set_title("scaffold-note");
    auto* step = seq.add_steps();
    step->set_method("/notes.Notes/PutNote");
    step->set_request_type("notes.PutNoteRequest");
    step->set_principal_id("alice");
    step->mutable_metadata()->emplace("x-user-id", "alice");
    step->set_request(req.SerializeAsString());
  }

  return seq;
}

}  // namespace grpc_authz_fuzz::invoker
