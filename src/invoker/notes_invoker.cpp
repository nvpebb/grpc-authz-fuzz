#include "src/invoker/notes_invoker.h"

#include <utility>

#include "notes/notes.grpc.pb.h"

namespace grpc_authz_fuzz::invoker {
namespace {

bool IsTextMetadata(const std::string& value) {
  for (unsigned char c : value) {
    if (c < 0x20 || c > 0x7e) {
      return false;
    }
  }
  return true;
}

bool ApplyMetadata(grpc::ClientContext* ctx, const Step& step, std::string* error) {
  if (!IsTextMetadata(step.principal_id())) {
    *error = "principal_id must be printable ASCII metadata";
    return false;
  }
  for (const auto& [key, value] : step.metadata()) {
    bool valid_key = !key.empty();
    for (unsigned char c : key) {
      valid_key = valid_key && ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                               c == '_' || c == '-' || c == '.');
    }
    if (!valid_key || (!key.ends_with("-bin") && !IsTextMetadata(value))) {
      *error = "invalid metadata: " + key;
      return false;
    }
  }
  const auto user = step.metadata().find("x-user-id");
  if (user != step.metadata().end() && user->second != step.principal_id()) {
    *error = "x-user-id conflicts with principal_id";
    return false;
  }
  for (const auto& [key, value] : step.metadata()) {
    if (key != "x-user-id") {
      ctx->AddMetadata(key, value);
    }
  }
  if (!step.principal_id().empty()) {
    ctx->AddMetadata("x-user-id", step.principal_id());
  }
  return true;
}

template <typename Request, typename Response, typename Call>
bool Invoke(const Step& step, StepResult* result, std::string* error, Call call) {
  Request request;
  if (step.request_type() != request.GetTypeName()) {
    *error = "request_type mismatch for " + step.method();
    return false;
  }
  if (!request.ParseFromString(step.request())) {
    *error = "failed to parse " + request.GetTypeName();
    return false;
  }
  Response response;
  const auto status = call(request, &response);
  result->status_code = static_cast<int>(status.error_code());
  result->status_message = status.error_message();
  result->response_type = response.GetTypeName();
  result->response = response.SerializeAsString();
  return true;
}

bool MethodIs(const std::string& method, const char* suffix) {
  return method == suffix || method == std::string("/") + suffix;
}

}  // namespace

NotesInvoker::NotesInvoker(std::chrono::milliseconds rpc_timeout)
    : server_(std::make_unique<targets::notes::NotesInProcessServer>()),
      rpc_timeout_(rpc_timeout) {
  stub_ = notes::Notes::NewStub(server_->channel());
}

NotesInvoker::NotesInvoker(std::shared_ptr<grpc::Channel> channel,
                           std::chrono::milliseconds rpc_timeout)
    : stub_(notes::Notes::NewStub(std::move(channel))), rpc_timeout_(rpc_timeout) {}

bool NotesInvoker::Reset(std::string* error) {
  if (server_) {
    server_->Reset();
    return true;
  }
  notes::ResetRequest req;
  notes::ResetResponse resp;
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() + rpc_timeout_);
  const auto status = stub_->Reset(&ctx, req, &resp);
  if (!status.ok() && error) {
    *error = "reset failed: " + status.error_message();
  }
  return status.ok();
}

ExecutionResult NotesInvoker::Execute(const Sequence& sequence) {
  ExecutionResult result;
  if (sequence.target_id() != "notes") {
    result.error = "unsupported target_id: " + sequence.target_id();
    return result;
  }
  if (!sequence.bindings().empty()) {
    result.error = "bindings are not implemented yet";
    return result;
  }
  for (int i = 0; i < sequence.steps_size(); ++i) {
    StepResult step_result;
    step_result.step_index = static_cast<uint32_t>(i);
    if (!ExecuteStep(sequence.steps(i), &step_result, &result.error)) {
      result.error = "step " + std::to_string(i) + ": " + result.error;
      break;
    }
    // Continue after non-OK RPC outcomes; oracles need to observe them too.
    result.steps.push_back(std::move(step_result));
  }
  return result;
}

bool NotesInvoker::ExecuteStep(const Step& step, StepResult* result, std::string* error) {
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() + rpc_timeout_);
  if (!ApplyMetadata(&ctx, step, error)) {
    return false;
  }

  if (MethodIs(step.method(), "notes.Notes/Echo")) {
    return Invoke<notes::EchoRequest, notes::EchoResponse>(
        step, result, error, [&](const auto& req, auto* resp) {
          return stub_->Echo(&ctx, req, resp);
        });
  }

  if (MethodIs(step.method(), "notes.Notes/PutNote")) {
    return Invoke<notes::PutNoteRequest, notes::PutNoteResponse>(
        step, result, error, [&](const auto& req, auto* resp) {
          return stub_->PutNote(&ctx, req, resp);
        });
  }

  if (MethodIs(step.method(), "notes.Notes/GetNote")) {
    return Invoke<notes::GetNoteRequest, notes::GetNoteResponse>(
        step, result, error, [&](const auto& req, auto* resp) {
          return stub_->GetNote(&ctx, req, resp);
        });
  }

  if (MethodIs(step.method(), "notes.Notes/Reset")) {
    return Invoke<notes::ResetRequest, notes::ResetResponse>(
        step, result, error, [&](const auto& req, auto* resp) {
          return stub_->Reset(&ctx, req, resp);
        });
  }

  *error = "unknown method: " + step.method();
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
    step->set_request(req.SerializeAsString());
  }

  {
    notes::PutNoteRequest req;
    req.set_title("scaffold-note");
    auto* step = seq.add_steps();
    step->set_method("/notes.Notes/PutNote");
    step->set_request_type("notes.PutNoteRequest");
    step->set_principal_id("alice");
    step->set_request(req.SerializeAsString());
  }

  return seq;
}

}  // namespace grpc_authz_fuzz::invoker
