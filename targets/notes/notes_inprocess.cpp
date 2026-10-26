#include "targets/notes/notes_inprocess.h"

#include <stdexcept>

namespace grpc_authz_fuzz::targets::notes {

NotesInProcessServer::NotesInProcessServer() {
  grpc::ServerBuilder builder;
  builder.RegisterService(&service_);
  server_ = builder.BuildAndStart();
  if (!server_) {
    throw std::runtime_error("failed to start Notes in-process server");
  }
  channel_ = server_->InProcessChannel(grpc::ChannelArguments());
}

NotesInProcessServer::~NotesInProcessServer() {
  if (server_) {
    server_->Shutdown();
  }
}

}  // namespace grpc_authz_fuzz::targets::notes
