include_directories("${CMAKE_CURRENT_SOURCE_DIR}")
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

set_source_files_properties(common/firebuild_common.c PROPERTIES COMPILE_FLAGS "-fPIC")
set_source_files_properties(fbbcomm.c PROPERTIES COMPILE_FLAGS "-fPIC")

add_custom_command(
  OUTPUT fbbcomm.c fbbcomm.h fbbcomm_decode.c
  DEPENDS common/fbbcomm.def
  common/fbb/generate_fbb
  common/fbb/tpl.c
  common/fbb/tpl.h
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/common
  COMMAND ./fbb/generate_fbb fbbcomm ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Generating fbbcomm files")

add_custom_target(fbbcomm_gen_files ALL DEPENDS fbbcomm.c fbbcomm.h fbbcomm_decode.c)

add_library(common_objs OBJECT common/firebuild_common.c fbbcomm.c)

add_subdirectory(interceptor)
add_subdirectory(firebuild)
