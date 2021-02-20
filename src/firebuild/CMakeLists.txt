
# GCC raises warning about the GCC-optimized code
set_source_files_properties(execed_process_cacher.cc obj_cache.cc utils.cc PROPERTIES COMPILE_FLAGS "-Wno-strict-overflow")

set_source_files_properties(firebuild.cc PROPERTIES COMPILE_FLAGS "-DFIREBUILD_VERSION='\"${FIREBUILD_VERSION}\"'")

find_package(FlatBuffers REQUIRED)
find_package(LibConfig REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(fmt REQUIRED)
pkg_check_modules(LIBEVENT REQUIRED libevent libevent_pthreads)
pkg_check_modules(JEMALLOC REQUIRED jemalloc)

add_custom_command(
  OUTPUT cache_object_format_generated.h
  DEPENDS cache_object_format.fbs
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND flatc -c --reflect-names ${CMAKE_CURRENT_SOURCE_DIR}/cache_object_format.fbs
  COMMENT "Generating Cache FlatBuffers files")

add_custom_target(flatbuffers_gen_files DEPENDS cache_object_format_generated.h)

add_executable(firebuild
  config.cc
  debug.cc
  file_name.cc
  firebuild.cc
  pipe.cc
  pipe_recorder.cc
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
  file_fd.cc
  file_usage.cc
  blob_cache.cc
  obj_cache.cc
  utils.cc
  $<TARGET_OBJECTS:common_objs>)
target_link_libraries(firebuild ${LIBCONFIGPP_LIBRARY} ${LIBEVENT_LIBRARIES} ${JEMALLOC_LIBRARIES} xxhash fmt::fmt pthread rt)
target_link_options(firebuild PUBLIC -Wno-array-bounds -Wno-strict-overflow)
set_property(TARGET firebuild PROPERTY INTERPROCEDURAL_OPTIMIZATION True)
add_dependencies(firebuild fbb_gen_files flatbuffers_gen_files)

install(TARGETS firebuild DESTINATION bin)
