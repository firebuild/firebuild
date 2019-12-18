
find_package(Protobuf REQUIRED)
find_package(LibConfig REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIRS})
PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS msg/fb-messages.proto)

set_source_files_properties(${PROTO_SRCS} PROPERTIES COMPILE_FLAGS "-Wno-effc++ -Wno-unused-parameter")

add_library(fbintercept MODULE
  firebuild_common.cc
  intercept.cc
  env.c
  ic_file_ops.cc
  ic_redirect_only.cc
  ${PROTO_SRCS})

target_link_libraries(fbintercept ${PROTOBUF_LIBRARIES} dl)

add_executable(firebuild
  firebuild.cc
  firebuild_common.cc
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
  ${PROTO_SRCS}
  ${PROTO_HDRS})
target_link_libraries(firebuild ${PROTOBUF_LIBRARIES} ${LIBCONFIGPP_LIBRARY} xxhash)
