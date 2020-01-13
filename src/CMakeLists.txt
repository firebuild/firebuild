find_package(Protobuf REQUIRED)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}")
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

include_directories(${PROTOBUF_INCLUDE_DIRS})

PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS common/msg/fb-messages.proto)
set_source_files_properties(${PROTO_SRCS} PROPERTIES COMPILE_FLAGS "-fPIC -Wno-effc++ -Wno-unused-parameter")

add_library(common_objs OBJECT ${PROTO_SRCS} common/firebuild_common.cc)

add_subdirectory(interceptor)
add_subdirectory(firebuild)
