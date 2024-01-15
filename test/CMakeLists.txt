add_custom_command(
  OUTPUT fbbtest.cc fbbtest.h fbbtest_decode.c
  DEPENDS fbbtest.def
  ../src/common/fbb/generate_fbb
  ../src/common/fbb/tpl.c
  ../src/common/fbb/tpl.h
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMAND ../src/common/fbb/generate_fbb fbbtest ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Generating fbbtest files")

add_custom_target(fbbtest_gen_files ALL DEPENDS fbbtest.cc fbbtest.h fbbtest_decode.c)
add_custom_target(check-bins)

function(add_test_binary TEST_BINARY)
  add_executable("${TEST_BINARY}" EXCLUDE_FROM_ALL "${TEST_BINARY}.c")
  add_dependencies(check-bins "${TEST_BINARY}")
endfunction()

foreach(TESTFILE empty-config.conf integration.bats print_tbd_lib_arch_exports test_parallel_make.Makefile test_symbols)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${TESTFILE}" "${CMAKE_CURRENT_BINARY_DIR}/${TESTFILE}" COPYONLY)
endforeach()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/test_helper.bash.in ${CMAKE_CURRENT_BINARY_DIR}/test_helper.bash @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/run-firebuild.in ${CMAKE_CURRENT_BINARY_DIR}/run-firebuild)

add_test(bats-integration ./integration.bats)
# firebuild's debug build would crash on the invalid entries in an assert()
if (uppercase_CMAKE_BUILD_TYPE STREQUAL "DEBUG" OR CMAKE_C_FLAGS MATCHES "-DFB_EXTRA_DEBUG")
  set_property(TEST bats-integration PROPERTY ENVIRONMENT "SKIP_GC_INVALID_ENTRIES_TEST=1")
endif()
add_custom_target(check ctest -V)
add_custom_target(valgrind-check env FIREBUILD_PREFIX_CMD='valgrind -q --leak-check=full --track-fds=yes --error-exitcode=1' ctest -V)

add_test_binary(test_cmd_fork_exec)
add_test_binary(test_cmd_popen)
add_test_binary(test_cmd_posix_spawn)
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
add_test_binary(test_cmd_clone)
endif()
add_test_binary(test_cmd_system)
add_test_binary(test_pthreads)
target_link_libraries(test_pthreads "-lpthread")
add_test_binary(test_system)
add_test_binary(test_exec)
if (SANITIZE)
  set_source_files_properties(test_file_ops.c PROPERTIES COMPILE_DEFINITIONS "SKIP_TEST_NULL_NONNULL_PARAMS=1")
endif()
add_test_binary(test_file_ops)
if(APPLE)
  set_source_files_properties(test_file_ops.c PROPERTIES COMPILE_FLAGS "-Wno-deprecated-declarations")
endif()
target_link_libraries(test_file_ops "-ldl")
add_library(test_dlopen_lib SHARED
  EXCLUDE_FROM_ALL
  test_dlopen_lib.c)
add_dependencies(check-bins test_dlopen_lib)
add_test_binary(test_file_ops_2)
add_test_binary(test_file_ops_3)
add_test_binary(test_random)
set_source_files_properties(test_sendfile.c PROPERTIES COMPILE_FLAGS "-Wno-deprecated-declarations")
add_test_binary(test_sendfile)
add_test_binary(test_wait)
add_test_binary(test_orphan)
add_test_binary(close_fds_exec)
add_test_binary(test_err)
if(NOT APPLE)
  add_test_binary(test_error)
endif()
add_test_binary(test_env_fixup)

if(NOT APPLE)
add_test_binary(test_static)
target_link_libraries(test_static "-static")
endif()

include_directories(${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/src ${CMAKE_CURRENT_BINARY_DIR})
file(CREATE_LINK ${CMAKE_SOURCE_DIR}/data "${CMAKE_BINARY_DIR}/test/data" SYMBOLIC)
set_source_files_properties(fbb_test.cc PROPERTIES COMPILE_FLAGS "-Wno-cast-align")
add_executable(fbb_test EXCLUDE_FROM_ALL fbb_test.cc fbbtest.cc)
add_dependencies(fbb_test fbbtest_gen_files)
add_test(fbb ./fbb_test)
add_custom_target(check-deps)
add_dependencies(check-deps fbb_test)
add_dependencies(check-deps firebuild)
add_dependencies(check-deps firebuild-bin)
add_dependencies(check check-deps check-bins)
add_dependencies(valgrind-check check-deps check-bins)

if(APPLE)
  add_test(test-symbols env CMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT} ./test_symbols ${CMAKE_BINARY_DIR})
else()
  add_test(test-symbols ./test_symbols ${CMAKE_BINARY_DIR})
endif()
