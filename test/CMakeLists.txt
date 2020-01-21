
function(add_test_binary TEST_BINARY)
  add_executable("${TEST_BINARY}" EXCLUDE_FROM_ALL "${TEST_BINARY}.c")
  add_dependencies(check "${TEST_BINARY}")
endfunction()

add_test(bats-integration ./integration.bats)

add_custom_target(check ctest -V)

add_test_binary(test_system)
add_test_binary(test_exec)
