add_library(grpc_authz_fuzz_engine
  engine/engine.cpp
  engine/coverage_shm.cpp
  engine/ipc.cpp
  engine/worker.cpp
  engine/pool.cpp
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

add_executable(grpc-authz-fuzz
  cli/main.cpp
)
target_link_libraries(grpc-authz-fuzz
  PRIVATE
    grpc_authz_fuzz_engine
    CLI11::CLI11
    yaml-cpp::yaml-cpp
)
