include_directories("${CMAKE_CURRENT_SOURCE_DIR}")
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

set_source_files_properties(common/debug_sysflags.c PROPERTIES COMPILE_FLAGS "-fPIC ${FAT_ARCH_C_FLAGS}")
set_source_files_properties(common/contrib/heapsort.c common/firebuild_common.c PROPERTIES COMPILE_FLAGS "-fPIC ${FAT_ARCH_C_FLAGS}")
set_source_files_properties(fbbcomm.c PROPERTIES COMPILE_FLAGS "-fPIC ${FAT_ARCH_C_FLAGS}")

add_custom_command(
  OUTPUT fbbcomm.c fbbcomm.cc fbbcomm.h fbbcomm_decode.c
  DEPENDS common/fbbcomm.def
  common/fbb/generate_fbb
  common/fbb/tpl.c
  common/fbb/tpl.h
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/common
  COMMAND ./fbb/generate_fbb fbbcomm ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Generating fbbcomm files")

add_custom_target(fbbcomm_gen_files ALL DEPENDS fbbcomm.c fbbcomm.cc fbbcomm.h fbbcomm_decode.c)

set(COMMON_OBJS_SOURCES common/firebuild_common.c common/debug_sysflags.c)
#if(APPLE)
  set(COMMON_OBJS_SOURCES common/contrib/heapsort.c ${COMMON_OBJS_SOURCES})
#endif()
add_library(common_objs OBJECT ${COMMON_OBJS_SOURCES})
add_library(fbbcomm_c OBJECT fbbcomm.c)
add_dependencies(fbbcomm_c fbbcomm_gen_files)
add_library(fbbcomm_cc OBJECT fbbcomm.cc)
add_dependencies(fbbcomm_cc fbbcomm_gen_files)

add_subdirectory(interceptor)
add_subdirectory(firebuild)

execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpmachine
                OUTPUT_VARIABLE C_COMPILER_TARGET_ARCH
                OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REGEX REPLACE "-.*" "" C_COMPILER_TARGET_ARCH ${C_COMPILER_TARGET_ARCH})
configure_file(${CMAKE_SOURCE_DIR}/src/common/config.h.in ${CMAKE_BINARY_DIR}/src/common/config.h @ONLY)
