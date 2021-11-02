set_source_files_properties(fbbtest.c PROPERTIES COMPILE_FLAGS "-fPIC")

add_custom_command(
  OUTPUT fbbtest.c fbbtest.h fbbtest_decode.c
  DEPENDS fbbtest.def
  ../src/common/fbb/generate_fbb
  ../src/common/fbb/tpl.c
  ../src/common/fbb/tpl.h
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMAND ../src/common/fbb/generate_fbb fbbtest ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Generating fbbtest files")

add_custom_target(fbbtest_gen_files ALL DEPENDS fbbtest.c fbbtest.h fbbtest_decode.c)

function(add_test_binary TEST_BINARY)
  add_executable("${TEST_BINARY}" EXCLUDE_FROM_ALL "${TEST_BINARY}.c")
  add_dependencies(check "${TEST_BINARY}")
endfunction()

file(COPY integration.bats test_parallel_make.Makefile test_symbols
  DESTINATION "${CMAKE_BINARY_DIR}/test")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/test_helper.bash.in ${CMAKE_CURRENT_BINARY_DIR}/test_helper.bash)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/run-firebuild.in ${CMAKE_CURRENT_BINARY_DIR}/run-firebuild)

add_test(bats-integration ./integration.bats)

add_custom_target(check ctest -V)
add_custom_target(valgrind-check env FIREBUILD_PREFIX_CMD='valgrind -q --leak-check=full --track-fds=yes --error-exitcode=1' ctest -V)

add_test_binary(test_cmd_fork_exec)
add_test_binary(test_cmd_popen)
add_test_binary(test_cmd_posix_spawn)
add_test_binary(test_cmd_system)
add_test_binary(test_system)
add_test_binary(test_exec)
add_test_binary(test_file_ops)
add_test_binary(test_file_ops_2)
add_test_binary(test_file_ops_3)
add_test_binary(test_wait)
add_test_binary(close_fds_exec)
add_test_binary(test_err)
add_test_binary(test_error)
add_test_binary(test_env_fixup)

add_test_binary(test_static)
target_link_libraries(test_static "-static")

include_directories(${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/src ${CMAKE_CURRENT_BINARY_DIR})
file(CREATE_LINK ${CMAKE_SOURCE_DIR}/data "${CMAKE_BINARY_DIR}/test/data" SYMBOLIC)
add_executable(fbb_test EXCLUDE_FROM_ALL fbb_test.cc fbbtest.c)
add_dependencies(fbb_test fbbtest_gen_files)
add_test(fbb ./fbb_test)
add_custom_target(check-deps)
add_dependencies(check-deps fbb_test)
add_dependencies(check-deps firebuild)
add_dependencies(check-deps firebuild-bin)
add_dependencies(check check-deps)
add_dependencies(valgrind-check check-deps)

add_test(test-symbols ./test_symbols ${CMAKE_BINARY_DIR})
