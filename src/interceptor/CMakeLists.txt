include_directories("${CMAKE_CURRENT_BINARY_DIR}")

# gnu89 required to avoid the __isoc99_scanf hell
# ignored warnings most likely don't exist in every compiler version
set_source_files_properties(interceptors.c PROPERTIES COMPILE_FLAGS "-std=gnu89 -Wno-pragmas -Wno-unknown-warning-option -Wno-deprecated-declarations ${FAT_ARCH_C_FLAGS}")

# the trace markers may truncate the marker messages
set_source_files_properties(intercept.c
  PROPERTIES COMPILE_FLAGS "-Wno-unknown-warning-option -Wno-stringop-truncation -DFIREBUILD_VERSION='\"${FIREBUILD_VERSION}\"' ${FAT_ARCH_C_FLAGS}")

if (SANITIZE)
  set(SANITIZE_INTERCEPTOR_LINK_OPTIONS -fsanitize=undefined)
endif()

add_custom_command (
  OUTPUT gen_decl.h gen_def.c gen_impl.c gen_impl_syscalls.c.inc gen_list.txt gen_reset.c
  DEPENDS generate_interceptors
  tpl.c.jinja2
  tpl_clone.c.jinja2
  tpl_closefrom.c.jinja2
  tpl_copy_file_range.c.jinja2
  tpl_close_range.c.jinja2
  tpl_dlopen.c.jinja2
  tpl_dup2.c.jinja2
  tpl_error.c.jinja2
  tpl_exec.c.jinja2
  tpl__exit.c.jinja2
  tpl_exit.c.jinja2
  tpl_fcntl.c.jinja2
  tpl_fork.c.jinja2
  tpl__fork.c.jinja2
  tpl_ioctl.c.jinja2
  tpl_marker_only.c.jinja2
  tpl_once.c.jinja2
  tpl_open.c.jinja2
  tpl_pclose.c.jinja2
  tpl_pipe.c.jinja2
  tpl_popen.c.jinja2
  tpl_posix_spawn.c.jinja2
  tpl_posix_spawn_file_actions.c.jinja2
  tpl_pthread_create.c.jinja2
  tpl_read.c.jinja2
  tpl_readlink.c.jinja2
  tpl_recvmsg.c.jinja2
  tpl_seek.c.jinja2
  tpl_shm_open.c.jinja2
  tpl_signal.c.jinja2
  tpl_skip.c.jinja2
  tpl_syscall.c.jinja2
  tpl_system.c.jinja2
  tpl_write.c.jinja2
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMAND ./generate_interceptors ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Generating interceptor files from templates")

add_custom_target(gen_files ALL DEPENDS gen_decl.h gen_def.c gen_impl.c gen_impl_syscalls.c.inc gen_list.txt gen_reset.c)

set_source_files_properties(
  env.c
  ic_file_ops.c
  time64_cast.c
  PROPERTIES COMPILE_FLAGS "${FAT_ARCH_C_FLAGS}")

add_library(firebuild SHARED
  env.c
  ic_file_ops.c
  intercept.c
  interceptors.c
  time64_cast.c
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
