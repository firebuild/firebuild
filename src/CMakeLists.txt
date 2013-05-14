
find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})
PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS fb-messages.proto)

add_library(fbintercept MODULE
  ${PROTO_SRCS})

target_link_libraries(fbintercept ${PROTOBUF_LIBRARIES})

add_executable(firebuild firebuild.cc ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(firebuild ${PROTOBUF_LIBRARIES})
