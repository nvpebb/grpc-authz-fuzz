# Corpus + worker IPC schema — protobuf only (no gRPC service).
grpc_authz_add_proto_library(
  grpc_authz_fuzz_sequence_proto
  PROTO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}"
  PROTOS
    "${CMAKE_CURRENT_SOURCE_DIR}/sequence.proto"
)

add_library(grpc_authz_fuzz_protos INTERFACE)
target_link_libraries(grpc_authz_fuzz_protos INTERFACE
  grpc_authz_fuzz_sequence_proto
)
