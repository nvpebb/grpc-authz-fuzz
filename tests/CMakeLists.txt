add_executable(sequence_roundtrip_test sequence_roundtrip_test.cpp)
target_link_libraries(sequence_roundtrip_test
  PRIVATE
    grpc_authz_fuzz_protos
    GTest::gtest_main
)
add_test(NAME sequence_roundtrip_test COMMAND sequence_roundtrip_test)

add_executable(notes_inprocess_test notes_inprocess_test.cpp)
target_link_libraries(notes_inprocess_test
  PRIVATE
    notes_target
    GTest::gtest_main
)
add_test(NAME notes_inprocess_test COMMAND notes_inprocess_test)

add_executable(notes_invoker_test notes_invoker_test.cpp)
target_link_libraries(notes_invoker_test
  PRIVATE
    grpc_authz_fuzz_engine
    GTest::gtest_main
)
add_test(NAME notes_invoker_test COMMAND notes_invoker_test)

add_executable(coverage_shm_test coverage_shm_test.cpp)
target_link_libraries(coverage_shm_test
  PRIVATE
    grpc_authz_fuzz_engine
    GTest::gtest_main
)
add_test(NAME coverage_shm_test COMMAND coverage_shm_test)

add_executable(pool_smoke_test pool_smoke_test.cpp)
target_link_libraries(pool_smoke_test
  PRIVATE
    grpc_authz_fuzz_engine
    GTest::gtest
)
add_test(
  NAME pool_smoke_test
  COMMAND pool_smoke_test --bin $<TARGET_FILE:grpc-authz-fuzz>
)
set_tests_properties(pool_smoke_test PROPERTIES
  ENVIRONMENT "GRPC_AUTHZ_FUZZ_BIN=$<TARGET_FILE:grpc-authz-fuzz>"
)

add_executable(worker_test worker_test.cpp)
target_link_libraries(worker_test
  PRIVATE
    grpc_authz_fuzz_engine
    GTest::gtest
)
add_test(NAME worker_test COMMAND worker_test)
set_tests_properties(pool_smoke_test worker_test notes_invoker_test notes_inprocess_test
  PROPERTIES TIMEOUT 30)

add_test(NAME cli_rejects_oracles COMMAND grpc-authz-fuzz run --oracles bola)
add_test(NAME cli_rejects_config COMMAND grpc-authz-fuzz run --config missing.yaml)
set_tests_properties(cli_rejects_oracles cli_rejects_config PROPERTIES WILL_FAIL TRUE TIMEOUT 5)

add_executable(ipc_test ipc_test.cpp)
target_link_libraries(ipc_test
  PRIVATE
    grpc_authz_fuzz_engine
    GTest::gtest_main
)
add_test(NAME ipc_test COMMAND ipc_test)
set_tests_properties(ipc_test PROPERTIES TIMEOUT 5)
