#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "notes/notes.grpc.pb.h"

namespace grpc_authz_fuzz::targets::notes {

struct Note {
  std::string note_id;
  std::string title;
  std::string owner;
};

// in-memory fixture for the Notes synthetic target
class NotesStore {
 public:
  void Reset();

  std::string PutNote(std::string title, std::string owner);
  std::optional<Note> GetNote(const std::string& note_id) const;

 private:
  mutable std::mutex mu_;
  std::uint64_t next_id_{1};
  std::unordered_map<std::string, Note> notes_;
};

class NotesServiceImpl final : public ::notes::Notes::Service {
 public:
  explicit NotesServiceImpl(NotesStore* store) : store_(store) {}

  grpc::Status Echo(grpc::ServerContext* context,
                    const ::notes::EchoRequest* request,
                    ::notes::EchoResponse* response) override;

  grpc::Status PutNote(grpc::ServerContext* context,
                       const ::notes::PutNoteRequest* request,
                       ::notes::PutNoteResponse* response) override;

  grpc::Status GetNote(grpc::ServerContext* context,
                       const ::notes::GetNoteRequest* request,
                       ::notes::GetNoteResponse* response) override;

  grpc::Status Reset(grpc::ServerContext*,
                     const ::notes::ResetRequest*,
                     ::notes::ResetResponse*) override;

 private:
  static std::string PrincipalFromContext(grpc::ServerContext* context);

  NotesStore* store_;
};

}  // namespace grpc_authz_fuzz::targets::notes
