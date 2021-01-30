include_directories("${CMAKE_CURRENT_SOURCE_DIR}")
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

set_source_files_properties(common/firebuild_common.c PROPERTIES COMPILE_FLAGS "-fPIC")
set_source_files_properties(common/shmq.c PROPERTIES COMPILE_FLAGS "-fPIC")
set_source_files_properties(fbb.c PROPERTIES COMPILE_FLAGS "-fPIC")

add_custom_command(
  OUTPUT fbb.c fbb.h
  DEPENDS common/msg/generate_fbb
  common/msg/tpl.c
  common/msg/tpl.h
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/common/msg
  COMMAND ./generate_fbb ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Generating FBB files")

add_custom_target(fbb_gen_files ALL DEPENDS fbb.c fbb.h)

add_library(common_objs OBJECT common/firebuild_common.c common/shmq.c fbb.c)

add_subdirectory(interceptor)
add_subdirectory(firebuild)
