#pragma once

#include <memory>

#include <grpcpp/grpcpp.h>

#include "targets/notes/notes_service.h"

namespace grpc_authz_fuzz::targets::notes {

// owns an in-process gRPC server and store for the Notes target
class NotesInProcessServer {
 public:
  NotesInProcessServer();
  ~NotesInProcessServer();

  NotesInProcessServer(const NotesInProcessServer&) = delete;
  NotesInProcessServer& operator=(const NotesInProcessServer&) = delete;

  std::shared_ptr<grpc::Channel> channel() const { return channel_; }
  NotesStore* store() { return &store_; }
  void Reset() { store_.Reset(); }

 private:
  NotesStore store_;
  NotesServiceImpl service_{&store_};
  std::unique_ptr<grpc::Server> server_;
  std::shared_ptr<grpc::Channel> channel_;
};

}  // namespace grpc_authz_fuzz::targets::notes
