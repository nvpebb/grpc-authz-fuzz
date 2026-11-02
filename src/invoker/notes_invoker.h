#pragma once

#include <chrono>
#include <memory>

#include <grpcpp/grpcpp.h>

#include "src/invoker/rpc_invoker.h"
#include "targets/notes/notes_inprocess.h"

namespace grpc_authz_fuzz::invoker {

class NotesInvoker final : public RpcInvoker {
 public:
  explicit NotesInvoker(std::chrono::milliseconds rpc_timeout = std::chrono::seconds(2));
  explicit NotesInvoker(std::shared_ptr<grpc::Channel> channel,
                        std::chrono::milliseconds rpc_timeout = std::chrono::seconds(2));

  bool Reset(std::string* error) override;
  ExecutionResult Execute(const Sequence& sequence) override;

  // owned only when constructed with the default ctor (owns the server)
  targets::notes::NotesInProcessServer* server() { return server_.get(); }

 private:
  bool ExecuteStep(const Step& step, StepResult* result, std::string* error);

  std::unique_ptr<targets::notes::NotesInProcessServer> server_;
  std::unique_ptr<notes::Notes::Stub> stub_;
  std::chrono::milliseconds rpc_timeout_;
};

// builds a fixed Echo + PutNote sequence for pool smoke without corpus/LPM
Sequence MakeNotesSmokeSequence();

}  // namespace grpc_authz_fuzz::invoker
