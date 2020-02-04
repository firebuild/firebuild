project(FireBuild C CXX)

cmake_minimum_required(VERSION 2.8)

include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules)
# TODO set to install dir
set(DATADIR "${CMAKE_SOURCE_DIR}/data" CACHE STRING "FireBuild's architecture-independent files' location")
set(TENTATIVE_C_CXX_FLAGS
  -std=c++0x
  -W
  -Wall
  -Wextra
  -ggdb
  -Werror
  -Wpointer-arith
  -Warray-bounds
  -Wcast-align
  -Wformat -Wformat-security
  -fstrict-overflow -Wstrict-overflow=4
  -Wunreachable-code
  -Warray-bounds
  -fvisibility=hidden
  -Woverflow
  -Weffc++
#  -Wlogical-op clang does not like that
  -Wredundant-decls)

foreach(FLAG ${TENTATIVE_C_CXX_FLAGS})
  check_c_compiler_flag(${FLAG} V)
  if (${V})
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FLAG}")
  endif()
  check_cxx_compiler_flag(${FLAG} V_CXX)
  if (${V_CXX})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${FLAG}")
  endif()
endforeach()

if(COVERAGE)
  # minimal (lines) coverage
  set(MIN_COVERAGE "65")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -fprofile-arcs -ftest-coverage")
  set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -g -fprofile-arcs -ftest-coverage")
endif(COVERAGE)

add_definitions(-DFIREBUILD_DATADIR="${DATADIR}")
add_subdirectory(src)

enable_testing()
add_subdirectory(test)

# add a target to generate API documentation with Doxygen
find_package(Doxygen)
if(DOXYGEN_FOUND)
  configure_file(${CMAKE_SOURCE_DIR}/Doxyfile.in ${CMAKE_BINARY_DIR}/Doxyfile @ONLY)
  add_custom_target(doc
    ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating API documentation with Doxygen" VERBATIM)
endif(DOXYGEN_FOUND)

if(COVERAGE)
  add_custom_target(coverage_info
    sh -c "find . -name *.gcno | while read f ; do cp \$f $(dirname $f| xargs dirname | xargs dirname)/$(basename $f) ; done ; lcov -c -d . --no-external -o tmp.info && lcov -r tmp.info -o coverage.info '*.pb.*' '*test_exec.c*' -o coverage.info && genhtml coverage.info && lcov  --summary coverage.info  2>&1 | grep '^  lines' | sed 's/  lines\\.*: \\([^%]*\\)%.*/\\1 >= ${MIN_COVERAGE}/' | bc | xargs test 1 -eq"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating coverage report with lcov" VERBATIM)
endif(COVERAGE)
