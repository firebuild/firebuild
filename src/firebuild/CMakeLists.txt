PROTOBUF_GENERATE_CPP(PROTO_CACHE_SRCS PROTO_CACHE_HDRS msg/fb-cache.proto)
set_source_files_properties(${PROTO_CACHE_SRCS} PROPERTIES COMPILE_FLAGS "-Wno-effc++ -Wno-unused-parameter")

find_package(LibConfig REQUIRED)

add_executable(firebuild
  debug.cc
  firebuild.cc
  process.cc
  execed_process.cc
  execed_process_cacher.cc
  execed_process_parameters.cc
  forked_process.cc
  process_factory.cc
  process_tree.cc
  process_proto_adaptor.cc
  hash.cc
  file.cc
  file_db.cc
  file_usage.cc
  file_usage_db.cc
  cache.cc
  multi_cache.cc
  ${PROTO_CACHE_SRCS}
  ${PROTO_CACHE_HDRS}
  $<TARGET_OBJECTS:common_objs>)
target_link_libraries(firebuild ${PROTOBUF_LIBRARIES} ${LIBCONFIGPP_LIBRARY} xxhash)
