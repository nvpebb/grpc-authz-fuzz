add_executable(sequence_roundtrip_test sequence_roundtrip_test.cpp)
target_link_libraries(sequence_roundtrip_test
  PRIVATE
    grpc_authz_fuzz_protos
    GTest::gtest_main
)
add_test(NAME sequence_roundtrip_test COMMAND sequence_roundtrip_test)
