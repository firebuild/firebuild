find_package(LibConfig REQUIRED)

add_executable(firebuild
  firebuild.cc
  process.cc
  execed_process.cc
  execed_process_parameters.cc
  forked_process.cc
  process_factory.cc
  process_tree.cc
  process_proto_adaptor.cc
  hash.cc
  file.cc
  file_db.cc
  cache.cc
  multi_cache.cc
  $<TARGET_OBJECTS:common_objs>)
target_link_libraries(firebuild ${PROTOBUF_LIBRARIES} ${LIBCONFIGPP_LIBRARY} xxhash)
