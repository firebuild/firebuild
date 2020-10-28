include_directories("${CMAKE_CURRENT_BINARY_DIR}")

# gnu89 required to avoid the __isoc99_scanf hell
set_source_files_properties(interceptors.c PROPERTIES COMPILE_FLAGS "-std=gnu89")

# the trace markers may truncate the marker messages
set_source_files_properties(intercept.c PROPERTIES COMPILE_FLAGS "-Wno-unknown-warning-option -Wno-stringop-truncation")

add_custom_command (
  OUTPUT gen_decl.h gen_def.c gen_impl.c gen_init.c gen_list.txt gen_reset.c
  DEPENDS generate_interceptors
  tpl.c
  tpl_dlopen.c
  tpl_error.c
  tpl_exec.c
  tpl__exit.c
  tpl_exit.c
  tpl_fcntl.c
  tpl_fork.c
  tpl_ioctl.c
  tpl_main.c
  tpl_marker_only.c
  tpl_once.c
  tpl_open.c
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

add_library(fbintercept MODULE
  env.c
  ic_file_ops.c
  ic_redirect_only.c
  intercept.c
  interceptors.c
  $<TARGET_OBJECTS:common_objs>)

add_dependencies(fbintercept gen_files fbb_gen_files)

target_link_libraries(fbintercept dl)
target_link_options(fbintercept PUBLIC -Wno-strict-overflow)
set_property(TARGET fbintercept PROPERTY INTERPROCEDURAL_OPTIMIZATION True)
