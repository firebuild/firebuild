project(FireBuild C CXX)

cmake_minimum_required(VERSION 2.8)

include(CheckCCompilerFlag)

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

set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}")
add_subdirectory(src)
