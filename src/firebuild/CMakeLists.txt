
# GCC raises warning about the GCC-optimized code
set_source_files_properties(execed_process_cacher.cc obj_cache.cc utils.cc PROPERTIES COMPILE_FLAGS "-Wno-strict-overflow")

set_source_files_properties(firebuild.cc PROPERTIES COMPILE_FLAGS "-DFIREBUILD_VERSION='\"${FIREBUILD_VERSION}\"'")

find_package(LibConfig REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(fmt REQUIRED)
pkg_check_modules(LIBEVENT REQUIRED libevent)
pkg_check_modules(JEMALLOC REQUIRED jemalloc)
find_package(tsl-hopscotch-map REQUIRED)

add_custom_command(
  OUTPUT fbbfp.cc fbbfp.h fbbfp_decode.c
  DEPENDS fbbfp.def
  ../common/fbb/generate_fbb
  ../common/fbb/tpl.c
  ../common/fbb/tpl.h
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMAND ../common/fbb/generate_fbb fbbfp ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Generating fbbfp files")

add_custom_target(fbbfp_gen_files ALL DEPENDS fbbfp.cc fbbfp.h fbbfp_decode.c)

add_custom_command(
  OUTPUT fbbstore.cc fbbstore.h fbbstore_decode.c
  DEPENDS fbbstore.def
  ../common/fbb/generate_fbb
  ../common/fbb/tpl.c
  ../common/fbb/tpl.h
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMAND ../common/fbb/generate_fbb fbbstore ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Generating fbbstore files")

add_custom_target(fbbstore_gen_files ALL DEPENDS fbbstore.cc fbbstore.h fbbstore_decode.c)

add_executable(firebuild-bin
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
  fbbfp.cc
  fbbstore.cc
  $<TARGET_OBJECTS:common_objs>
  $<TARGET_OBJECTS:fbbcomm_cc>)
target_link_libraries(firebuild-bin ${LIBCONFIGPP_LIBRARY} ${LIBEVENT_LIBRARIES} ${JEMALLOC_LIBRARIES} xxhash fmt::fmt)
target_link_options(firebuild-bin PUBLIC -Wno-array-bounds -Wno-strict-overflow)
set_target_properties(firebuild-bin
  PROPERTIES INTERPROCEDURAL_OPTIMIZATION True OUTPUT_NAME firebuild)
add_dependencies(firebuild-bin fbbcomm_gen_files fbbfp_gen_files fbbstore_gen_files)

install(TARGETS firebuild-bin DESTINATION bin)
