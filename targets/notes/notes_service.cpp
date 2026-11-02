#include "targets/notes/notes_service.h"

#include <utility>

namespace grpc_authz_fuzz::targets::notes {
namespace {

constexpr char kUserMetadataKey[] = "x-user-id";

}  // namespace

void NotesStore::Reset() {
  std::lock_guard lock(mu_);
  notes_.clear();
  next_id_ = 1;
}

std::string NotesStore::PutNote(std::string title, std::string owner) {
  std::lock_guard lock(mu_);
  const auto id = std::to_string(next_id_++);
  notes_.emplace(id, Note{id, std::move(title), std::move(owner)});
  return id;
}

std::optional<Note> NotesStore::GetNote(const std::string& note_id) const {
  std::lock_guard lock(mu_);
  const auto it = notes_.find(note_id);
  if (it == notes_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::string NotesServiceImpl::PrincipalFromContext(grpc::ServerContext* context) {
  const auto& md = context->client_metadata();
  const auto it = md.find(kUserMetadataKey);
  if (it == md.end()) {
    return "anonymous";
  }
  return std::string(it->second.data(), it->second.length());
}

grpc::Status NotesServiceImpl::Echo(grpc::ServerContext*,
                                    const ::notes::EchoRequest* request,
                                    ::notes::EchoResponse* response) {
  response->set_message(request->message());
  return grpc::Status::OK;
}

grpc::Status NotesServiceImpl::PutNote(grpc::ServerContext* context,
                                       const ::notes::PutNoteRequest* request,
                                       ::notes::PutNoteResponse* response) {
  const auto owner = PrincipalFromContext(context);
  const auto id = store_->PutNote(request->title(), owner);
  response->set_note_id(id);
  return grpc::Status::OK;
}

grpc::Status NotesServiceImpl::GetNote(grpc::ServerContext*,
                                       const ::notes::GetNoteRequest* request,
                                       ::notes::GetNoteResponse* response) {
  const auto note = store_->GetNote(request->note_id());
  if (!note) {
    return {grpc::StatusCode::NOT_FOUND, "note not found"};
  }
  response->set_note_id(note->note_id);
  response->set_title(note->title);
  response->set_owner(note->owner);
  return grpc::Status::OK;
}

grpc::Status NotesServiceImpl::Reset(grpc::ServerContext*,
                                     const ::notes::ResetRequest*,
                                     ::notes::ResetResponse*) {
  store_->Reset();
  return grpc::Status::OK;
}

}  // namespace grpc_authz_fuzz::targets::notes
