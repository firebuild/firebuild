add_executable(firebuild-shim
  firebuild-shim.c)
set_property(TARGET firebuild-shim PROPERTY INTERPROCEDURAL_OPTIMIZATION True)
install(TARGETS firebuild-shim DESTINATION lib/firebuild)
install(DIRECTORY DESTINATION lib/firebuild/bin)
