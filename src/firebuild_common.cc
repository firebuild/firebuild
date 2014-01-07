
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
  int offset = 0, msg_size = pb_msg.ByteSize();
  char *buf = new char[sizeof(uint32_t) + msg_size];

  *(uint32_t*)(buf) = htonl(static_cast<uint32_t>(msg_size));
  offset += sizeof(uint32_t);

  pb_msg.SerializeWithCachedSizesToArray((uint8_t*)(&buf[offset]));
  auto ret = fb_write_buf(fd, buf, msg_size + offset);

  delete[] buf;
  return ret;
}


/**
 * Read protobuf message via file descriptor
 *
 * Framing is very simple: 4 bytes length, then the protobuf message serialized
 */
extern ssize_t fb_recv_msg (google::protobuf::MessageLite &pb_msg, const int fd)
{
  uint32_t msg_size;

  /* read serialized length */
  auto ret = fb_read_buf(fd, &msg_size, sizeof(msg_size));
  if (ret == -1 || ret == 0) {
    return ret;
  }
  msg_size = ntohl(msg_size);

  auto buf = new char[msg_size];
  /* read serialized msg */
  if (-1 == ((ret = fb_read_buf(fd, buf, msg_size)))) {
    delete[] buf;
    return ret;
  }

  pb_msg.ParseFromArray(buf, msg_size);

  delete[] buf;
  return ret;
}

} // namespace firebuild
