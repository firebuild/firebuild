project(FireBuild C CXX)

cmake_minimum_required(VERSION 2.8)

include(CheckCCompilerFlag)

set(TENTATIVE_C_FLAGS
  -Wextra
  -Werror
  -Wformat -Wformat-security
  -fstrict-overflow -Wstrict-overflow=4
  -Wunreachable-code
  -Wunsafe-loop-optimizations
  -Warray-bounds
  -fvisibility=hidden
  -Wredundant-decls)

foreach(FLAG ${TENTATIVE_C_FLAGS})
  check_c_compiler_flag(${FLAG} V)
  if (${V})
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FLAG}")
  endif()
endforeach()

set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}")
add_subdirectory(src)
