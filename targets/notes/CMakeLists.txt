add_library(notes_target
  notes_service.cpp
  notes_inprocess.cpp
)
target_include_directories(notes_target PUBLIC "${CMAKE_SOURCE_DIR}")
target_link_libraries(notes_target PUBLIC grpc_authz_fuzz_protos gRPC::grpc++)
