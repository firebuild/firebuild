
# GCC raises warning about the GCC-optimized code
set_source_files_properties(debug.cc execed_process.cc file_name.cc obj_cache.cc utils.cc PROPERTIES COMPILE_FLAGS "-Wno-strict-overflow")

# FBB structures and placing them aligned in buffers ensure that members will be aligned properly, too
set_source_files_properties(execed_process_cacher.cc PROPERTIES COMPILE_FLAGS "-Wno-cast-align -Wno-strict-overflow")
set_source_files_properties(message_processor.cc PROPERTIES COMPILE_FLAGS "-Wno-cast-align")

find_package(LibConfig REQUIRED)
find_package(PkgConfig REQUIRED)
# xxh128's algorithm changed and became officially supported in 0.8.0
pkg_check_modules(XXHASH REQUIRED libxxhash>=0.8.0)
if (WITH_JEMALLOC)
  pkg_check_modules(JEMALLOC jemalloc)
endif()
find_package(tsl-hopscotch-map REQUIRED)
if (APPLE)
  pkg_check_modules(PLIST REQUIRED libplist-2.0>=2.3.0)
  find_library(IOKit IOKit)
  find_library(CoreFoundation CoreFoundation)
else()
  pkg_check_modules(libelf REQUIRED libelf)
endif()
if (SANITIZE)
  if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # older GCCs needs the -static-libasan parameter to avoid ASan complaining with fakeroot
    set(SANITIZE_SUPERVISOR_LINK_OPTIONS -static-libasan -fsanitize=address -fsanitize=undefined)
  else()
    set(SANITIZE_SUPERVISOR_LINK_OPTIONS -fsanitize=address -fsanitize=undefined)
  endif()
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I${tsl-hopscotch-map_INCLUDE_DIRS}")
set_source_files_properties(config.cc PROPERTIES INCLUDE_DIRECTORIES "${LIBCONFIGPP_INCLUDE_DIR}")

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
  base64.cc
  command_rewriter.cc
  config.cc
  debug.cc
  epoll.cc
  file_name.cc
  firebuild.cc
  pipe.cc
  pipe_recorder.cc
  process.cc
  exe_matcher.cc
  execed_process.cc
  execed_process_cacher.cc
  execed_process_env.cc
  forked_process.cc
  message_processor.cc
  options.cc
  process_factory.cc
  process_tree.cc
  hash.cc
  hash_cache.cc
  file_fd.cc
  file_info.cc
  file_usage.cc
  file_usage_update.cc
  blob_cache.cc
  obj_cache.cc
  report.cc
  sigchild_callback.cc
  utils.cc
  fbbfp.cc
  fbbstore.cc
  $<TARGET_OBJECTS:common_objs>
  $<TARGET_OBJECTS:fbbcomm_cc>)
target_link_libraries(firebuild-bin ${LIBCONFIGPP_LIBRARY} ${JEMALLOC_LDFLAGS} ${XXHASH_LDFLAGS} ${libelf_LIBRARIES} ${PLIST_LINK_LIBRARIES} ${IOKit} ${CoreFoundation})
target_link_options(firebuild-bin PUBLIC -Wno-array-bounds -Wno-strict-overflow ${SANITIZE_SUPERVISOR_LINK_OPTIONS})
set_target_properties(firebuild-bin PROPERTIES OUTPUT_NAME firebuild)
# GCC 9's LTO implementation seem to have a bug we hit, but did not fully triage yet
# https://github.com/Tessil/hopscotch-map/issues/55
if ((NOT uppercase_CMAKE_BUILD_TYPE STREQUAL "DEBUG")
    AND NOT (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10))
  set_target_properties(firebuild-bin PROPERTIES INTERPROCEDURAL_OPTIMIZATION True)
endif()

add_dependencies(firebuild-bin fbbcomm_gen_files fbbfp_gen_files fbbstore_gen_files)

install(TARGETS firebuild-bin DESTINATION bin)
