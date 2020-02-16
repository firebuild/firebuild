
function(add_test_binary TEST_BINARY)
  add_executable("${TEST_BINARY}" EXCLUDE_FROM_ALL "${TEST_BINARY}.c")
  add_dependencies(check "${TEST_BINARY}")
endfunction()

add_test(bats-integration ./integration.bats)

add_custom_target(check ctest -V)
add_custom_target(valgrind-check env FIREBUILD_PREFIX_CMD='valgrind -q --leak-check=full --track-fds=yes --error-exitcode=1' ctest -V)

add_test_binary(test_system)
add_test_binary(test_exec)
add_test_binary(close_fds_exec)
