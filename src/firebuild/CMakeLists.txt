PROTOBUF_GENERATE_CPP(PROTO_CACHE_SRCS PROTO_CACHE_HDRS msg/fb-cache.proto)
set_source_files_properties(${PROTO_CACHE_SRCS} PROPERTIES COMPILE_FLAGS "-Wno-effc++ -Wno-unused-parameter -Wno-array-bounds")

# GCC raises warning about the GCC-optimized code
set_source_files_properties(execed_process_cacher.cc PROPERTIES COMPILE_FLAGS "-Wno-strict-overflow")

find_package(LibConfig REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVENT REQUIRED libevent)
pkg_check_modules(JEMALLOC REQUIRED jemalloc)

add_executable(firebuild
  config.cc
  debug.cc
  firebuild.cc
  pipe.cc
  process.cc
  execed_process.cc
  execed_process_cacher.cc
  execed_process_env.cc
  forked_process.cc
  process_factory.cc
  process_tree.cc
  process_proto_adaptor.cc
  hash.cc
  hash_cache.cc
  file.cc
  file_db.cc
  file_usage.cc
  file_usage_db.cc
  cache.cc
  multi_cache.cc
  utils.cc
  ${PROTO_CACHE_SRCS}
  ${PROTO_CACHE_HDRS}
  $<TARGET_OBJECTS:common_objs>)
target_link_libraries(firebuild ${PROTOBUF_LIBRARIES} ${LIBCONFIGPP_LIBRARY} ${LIBEVENT_LIBRARIES} ${JEMALLOC_LIBRARIES} xxhash)
target_link_options(firebuild PUBLIC -Wno-array-bounds -Wno-strict-overflow)
set_property(TARGET firebuild PROPERTY INTERPROCEDURAL_OPTIMIZATION True)
add_dependencies(firebuild fbb_gen_files)
