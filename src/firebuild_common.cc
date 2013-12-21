
#include "firebuild_common.h"

#include <netinet/in.h>

#include <cstdlib>

#include <google/protobuf/message_lite.h>

namespace firebuild {

/**
 * Send protobuf message via file descriptor
 *
 * Framing is very simple: 4 bytes length, then the protobuf message serialized
 */
extern ssize_t fb_send_msg (const google::protobuf::MessageLite &pb_msg, const int fd)
{
  ssize_t ret;
  char *buf = NULL;
  int offset = 0, msg_size = pb_msg.ByteSize();
  buf = static_cast<char*>(malloc(sizeof(uint32_t) + msg_size));

  *(uint32_t*)(buf) = htonl(static_cast<uint32_t>(msg_size));
  offset += sizeof(uint32_t);

  pb_msg.SerializeWithCachedSizesToArray((uint8_t*)(&buf[offset]));
  ret = fb_write_buf(fd, buf, msg_size + offset);

  free(buf);
  return ret;
}


/**
 * Read protobuf message via file descriptor
 *
 * Framing is very simple: 4 bytes length, then the protobuf message serialized
 */
extern ssize_t fb_recv_msg (google::protobuf::MessageLite &pb_msg, const int fd)
{
  ssize_t ret;
  char *buf = NULL;
  uint32_t msg_size;

  /* read serialized length */
  ret = fb_read_buf(fd, &msg_size, sizeof(uint32_t));
  if (ret == -1 || ret == 0) {
    return ret;
  }
  msg_size = ntohl(msg_size);

  buf = static_cast<char*>(malloc(msg_size));

  /* read serialized msg */
  if (-1 == ((ret = fb_read_buf(fd, buf, msg_size)))) {
    free(buf);
    return ret;
  }

  pb_msg.ParseFromArray(buf, msg_size);

  free(buf);
  return ret;
}

} // namespace firebuild
