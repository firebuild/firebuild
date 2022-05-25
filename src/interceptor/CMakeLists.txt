include_directories("${CMAKE_CURRENT_BINARY_DIR}")

# gnu89 required to avoid the __isoc99_scanf hell
# ignored warnings most likely don't exist in every compiler version
set_source_files_properties(interceptors.c PROPERTIES COMPILE_FLAGS "-std=gnu89 -Wno-pragmas -Wno-unknown-warning-option")

# the trace markers may truncate the marker messages
set_source_files_properties(intercept.c
  PROPERTIES COMPILE_FLAGS "-Wno-unknown-warning-option -Wno-stringop-truncation -DFIREBUILD_VERSION='\"${FIREBUILD_VERSION}\"'")

if (SANITIZE)
  set(SANITIZE_INTERCEPTOR_LINK_OPTIONS -fsanitize=undefined)
endif()

add_custom_command (
  OUTPUT gen_decl.h gen_def.c gen_impl.c gen_init.c gen_list.txt gen_reset.c
  DEPENDS generate_interceptors
  tpl.c
  tpl_clone.c
  tpl_closefrom.c
  tpl_close_range.c
  tpl_dlopen.c
  tpl_dup2.c
  tpl_error.c
  tpl_exec.c
  tpl__exit.c
  tpl_exit.c
  tpl_fcntl.c
  tpl_fork.c
  tpl_ioctl.c
  tpl_marker_only.c
  tpl_once.c
  tpl_open.c
  tpl_pclose.c
  tpl_pipe.c
  tpl_popen.c
  tpl_posix_spawn.c
  tpl_posix_spawn_file_actions.c
  tpl_pthread_create.c
  tpl_read.c
  tpl_readlink.c
  tpl_signal.c
  tpl_skip.c
  tpl_syscall.c
  tpl_system.c
  tpl_write.c
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMAND ./generate_interceptors ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Generating interceptor files from templates")

add_custom_target(gen_files ALL DEPENDS gen_decl.h gen_def.c gen_impl.c gen_init.c gen_list.txt gen_reset.c)

add_library(firebuild SHARED
  env.c
  ic_file_ops.c
  intercept.c
  interceptors.c
  $<TARGET_OBJECTS:common_objs>
  $<TARGET_OBJECTS:fbbcomm_c>)

add_dependencies(firebuild gen_files fbbcomm_gen_files)

set_target_properties(firebuild PROPERTIES VERSION "0.0.0" SOVERSION 0)
if (NOT uppercase_CMAKE_BUILD_TYPE STREQUAL "DEBUG")
  set_target_properties(firebuild PROPERTIES INTERPROCEDURAL_OPTIMIZATION True)
endif()
target_link_libraries(firebuild dl)
target_link_options(firebuild PUBLIC -Wno-strict-overflow ${SANITIZE_INTERCEPTOR_LINK_OPTIONS})

install(TARGETS firebuild DESTINATION ${CMAKE_INSTALL_LIBDIR})
