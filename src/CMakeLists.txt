add_library(grpc_authz_fuzz_engine
  invoker/notes_invoker.cpp
)
target_include_directories(grpc_authz_fuzz_engine PUBLIC "${CMAKE_SOURCE_DIR}")
target_link_libraries(grpc_authz_fuzz_engine
  PUBLIC
    grpc_authz_fuzz_protos
    notes_target
    spdlog::spdlog
    rt
)
