project(FireBuild C CXX)

cmake_minimum_required(VERSION 2.8)

include(CheckCCompilerFlag)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules)
# TODO set to install dir
set(DATADIR "${CMAKE_SOURCE_DIR}/data" CACHE STRING "FireBuild's architecture-independent files' location")
set(TENTATIVE_C_FLAGS
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
  -Wlogical-op
  -Wredundant-decls)

foreach(FLAG ${TENTATIVE_C_FLAGS})
  check_c_compiler_flag(${FLAG} V)
  if (${V})
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FLAG}")
  endif()
endforeach()

set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++0x")
add_definitions(-DFIREBUILD_DATADIR="${DATADIR}")
add_subdirectory(src)

# add a target to generate API documentation with Doxygen
find_package(Doxygen)
if(DOXYGEN_FOUND)
  configure_file(${CMAKE_SOURCE_DIR}/Doxyfile.in ${CMAKE_BINARY_DIR}/Doxyfile @ONLY)
  add_custom_target(doc
    ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating API documentation with Doxygen" VERBATIM)
endif(DOXYGEN_FOUND)