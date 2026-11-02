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
