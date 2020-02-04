set_source_files_properties(intercept.cc PROPERTIES OBJECT_DEPENDS ${PROTO_MESSAGES_HDRS})

add_library(fbintercept MODULE
  intercept.cc
  env.c
  ic_file_ops.cc
  ic_redirect_only.cc
  $<TARGET_OBJECTS:common_objs>)

target_link_libraries(fbintercept ${PROTOBUF_LIBRARIES} dl)
