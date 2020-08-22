
function(add_test_binary TEST_BINARY)
  add_executable("${TEST_BINARY}" EXCLUDE_FROM_ALL "${TEST_BINARY}.c")
  add_dependencies(check "${TEST_BINARY}")
endfunction()

add_test(bats-integration ./integration.bats)

add_custom_target(check ctest -V)
add_custom_target(valgrind-check env FIREBUILD_PREFIX_CMD='valgrind -q --leak-check=full --track-fds=yes --error-exitcode=1' ctest -V)

add_test_binary(test_system)
add_test_binary(test_exec)
add_test_binary(test_file_ops)
add_test_binary(test_file_ops_2)
add_test_binary(test_file_ops_3)
add_test_binary(test_wait)
add_test_binary(close_fds_exec)

include_directories(${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/src)
add_executable(fbb_test EXCLUDE_FROM_ALL fbb_test.cc $<TARGET_OBJECTS:common_objs>)
add_test(fbb ./fbb_test)
add_dependencies(fbb_test fbb_gen_files)
add_dependencies(check fbb_test)
