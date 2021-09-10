cmake_minimum_required(VERSION 3.9)

project(FireBuild C CXX)

if (NOT DEFINED FIREBUILD_VERSION)
  execute_process(COMMAND git describe OUTPUT_VARIABLE FIREBUILD_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(FIREBUILD_VERSION "Git ${FIREBUILD_VERSION}")
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_SUPPORT_ERROR)
if(NOT IPO_SUPPORTED)
  message(STATUS "IPO / LTO not supported: <${IPO_SUPPORT_ERROR}>")
endif()

include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(GNUInstallDirs)

set(LIBFIREBUILD_SO "libfirebuild.so")

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules)
# TODO set to install dir
set(DATADIR "${CMAKE_INSTALL_FULL_DATAROOTDIR}/firebuild" CACHE STRING "FireBuild's architecture-independent files' location")
set(TENTATIVE_C_CXX_FLAGS
  -std=c++17
  -W
  -Wall
  -Wextra
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
  -Wredundant-decls
  -Wno-format-zero-length)

foreach(FLAG ${TENTATIVE_C_CXX_FLAGS})
  string(MAKE_C_IDENTIFIER "C_FLAG_VALID_${FLAG}" V)
  check_c_compiler_flag("${FLAG}" ${V})
  if (${V})
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FLAG}")
  endif()
  string(MAKE_C_IDENTIFIER "CXX_FLAG_VALID_${FLAG}" V_CXX)
  check_cxx_compiler_flag(${FLAG} ${V_CXX})
  if (${V_CXX})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${FLAG}")
  endif()
endforeach()

if(COVERAGE)
  # minimal (lines) coverage
  set(MIN_COVERAGE "65")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -fprofile-arcs -ftest-coverage")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fprofile-arcs -ftest-coverage")
endif(COVERAGE)

add_definitions(-DFIREBUILD_DATADIR="${DATADIR}" -DLIBFIREBUILD_SO="${LIBFIREBUILD_SO}")
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
  add_custom_target(coverage-info
    tools/calculate-coverage
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating coverage report with lcov" VERBATIM)
endif(COVERAGE)

add_custom_target(style-check
  cpplint --quiet --recursive ${CMAKE_SOURCE_DIR}/src/ ${CMAKE_SOURCE_DIR}/test/)

file(COPY etc/firebuild.conf
  DESTINATION "${CMAKE_BINARY_DIR}/etc")

install(FILES etc/firebuild.conf DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}")
install(FILES data/build-report.html DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/firebuild")
