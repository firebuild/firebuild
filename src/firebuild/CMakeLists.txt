find_package(LibConfig REQUIRED)

add_executable(firebuild
  firebuild.cc
  Process.cc
  ExecedProcess.cc
  ExecedProcessParameters.cc
  ForkedProcess.cc
  ProcessFactory.cc
  ProcessTree.cc
  ProcessPBAdaptor.cc
  Hash.cc
  File.cc
  FileDB.cc
  $<TARGET_OBJECTS:common_objs>)
target_link_libraries(firebuild ${PROTOBUF_LIBRARIES} ${LIBCONFIGPP_LIBRARY} xxhash)
