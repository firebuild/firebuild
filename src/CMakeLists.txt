
find_package(Protobuf REQUIRED)
find_package(LibConfig REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})
PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS fb-messages.proto)

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
  ForkedProcess.cc
  ProcessTree.cc
  ${PROTO_SRCS}
  ${PROTO_HDRS})
target_link_libraries(firebuild ${PROTOBUF_LIBRARIES} ${LIBCONFIGPP_LIBRARY})
