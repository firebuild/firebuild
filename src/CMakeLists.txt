find_package(Protobuf REQUIRED)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}")
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

include_directories(${PROTOBUF_INCLUDE_DIRS})

PROTOBUF_GENERATE_CPP(PROTO_MESSAGES_SRCS PROTO_MESSAGES_HDRS common/msg/fb-messages.proto)
set_source_files_properties(${PROTO_MESSAGES_SRCS} PROPERTIES COMPILE_FLAGS "-fPIC -Wno-effc++ -Wno-unused-parameter")
set_source_files_properties(common/firebuild_common.cc PROPERTIES COMPILE_FLAGS "-fPIC")

add_library(common_objs OBJECT ${PROTO_MESSAGES_SRCS} common/firebuild_common.cc)

add_subdirectory(interceptor)
add_subdirectory(firebuild)
