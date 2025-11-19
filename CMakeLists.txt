cmake_minimum_required(VERSION 3.13)

project(Firebuild C CXX)

include(CMakeOptions.txt)

if (NOT DEFINED FIREBUILD_VERSION)
  # Set in tagged commits only
  set(FIREBUILD_VERSION "0.8.5.1")
endif()

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

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules)
# TODO set to install dir
set(DATADIR "${CMAKE_INSTALL_FULL_DATAROOTDIR}/firebuild" CACHE STRING "Firebuild's architecture-independent files' location")
set(TENTATIVE_C_CXX_FLAGS
  -std=c++20
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

if (NOT APPLE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Weffc++")
else()
  # Those warnings are raised when assigning values where 32 bits
  # are way more than enough.
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-shorten-64-to-32")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-shorten-64-to-32")
endif()

# dynamic library handling
if (APPLE)
  set(LD_PRELOAD "DYLD_INSERT_LIBRARIES")
  set(LIBFIREBUILD_SO "libfirebuild.dylib")
else()
  set(LD_PRELOAD "LD_PRELOAD")
  set(LIBFIREBUILD_SO "libfirebuild.so")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

if (APPLE)
  # build the interceptor for multiple architectures
  set(FAT_ARCH_C_FLAGS "-arch arm64 -arch arm64e -arch x86_64")
  set(CMAKE_SHARED_LINKER_FLAGS ${FAT_ARCH_C_FLAGS})
  # TODO build the supervisor, too, for multiple architectures
endif()

if(COVERAGE)
  # minimal (lines) coverage
  set(MIN_COVERAGE "65")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -fprofile-arcs -ftest-coverage")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fprofile-arcs -ftest-coverage")
endif(COVERAGE)

if (SANITIZE)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=undefined")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined")
endif()

if (ENABLE_XXH_INLINE_ALL)
  add_definitions(-DXXH_INLINE_ALL)
endif()
string(TOUPPER "${CMAKE_BUILD_TYPE}" uppercase_CMAKE_BUILD_TYPE)
if (uppercase_CMAKE_BUILD_TYPE STREQUAL "DEBUG")
  add_definitions(-DFB_EXTRA_DEBUG)
endif()

add_subdirectory(src)

enable_testing()
add_subdirectory(test)
add_subdirectory(man)

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

configure_file("${CMAKE_SOURCE_DIR}/etc/firebuild.conf" "${CMAKE_BINARY_DIR}/etc/firebuild.conf" COPYONLY)

install(FILES "${CMAKE_BINARY_DIR}/etc/firebuild.conf" DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}")
if (APPLE)
  install(FILES "${CMAKE_SOURCE_DIR}/etc/bash_completion.d/firebuild" DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/bash_completion.d")
else()
  install(FILES "${CMAKE_SOURCE_DIR}/etc/bash_completion.d/firebuild" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/bash-completion/completions")
endif()
install(FILES data/build-report.html DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/firebuild")
install(FILES data/firebuild-logo.svg DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/firebuild")
