include_directories("${CMAKE_CURRENT_BINARY_DIR}")

set_source_files_properties(intercept.cc PROPERTIES OBJECT_DEPENDS ${PROTO_MESSAGES_HDRS})

add_custom_command (
  OUTPUT gen_decl.h gen_def.c gen_impl.c gen_init.c gen_reset.c
  DEPENDS generate_interceptors tpl*
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMAND ./generate_interceptors ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Generating interceptor files from templates")

add_custom_target(gen_files ALL DEPENDS gen_decl.h gen_def.c gen_impl.c gen_init.c gen_reset.c)

add_library(fbintercept MODULE
  env.c
  ic_file_ops.cc
  ic_redirect_only.cc
  intercept.cc
  interceptors.cc
  $<TARGET_OBJECTS:common_objs>)

add_dependencies(fbintercept gen_files)

target_link_libraries(fbintercept ${PROTOBUF_LIBRARIES} dl)
