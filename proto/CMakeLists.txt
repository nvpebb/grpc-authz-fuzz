# Corpus + worker IPC schema — protobuf only (no gRPC service).
grpc_authz_add_proto_library(
  grpc_authz_fuzz_sequence_proto
  PROTO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
  PROTOS
    "${CMAKE_CURRENT_SOURCE_DIR}/sequence.proto"
    "${CMAKE_CURRENT_SOURCE_DIR}/worker.proto"
)

# Notes synthetic target — protobuf + gRPC stubs.
grpc_authz_add_proto_library(
  grpc_authz_fuzz_notes_proto
  WITH_GRPC
  PROTO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
  PROTOS
    "${CMAKE_CURRENT_SOURCE_DIR}/notes/notes.proto"
)

# Convenience alias used by the rest of the tree.
add_library(grpc_authz_fuzz_protos INTERFACE)
target_link_libraries(grpc_authz_fuzz_protos INTERFACE
  grpc_authz_fuzz_sequence_proto
  grpc_authz_fuzz_notes_proto
)
