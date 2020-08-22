/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include "firebuild/utils.h"

#include <string>
#include <cstdlib>

#include "./fbb.h"
#include "common/firebuild_common.h"
#include "firebuild/debug.h"

namespace firebuild {

/**
 * Read a message along with an ack_id. See common/msg/README_MSG_FRAME.txt for details.
 *
 * The message is not decoded yet, it's up to the caller. The raw payload is placed in a newly
 * allocated buffer which the caller must delete[].
 *
 * @param bufp updated with the pointer to the newly allocated buffer
 * @param ack_id_p if non-NULL, store the received ack_id here
 * @param fd the communication file descriptor
 * @return the received payload length (0 for empty messages), or -1 on error
 */
ssize_t fb_recv_msg(char **bufp, uint32_t *ack_id_p, int fd) {
  uint32_t msg_size;

  /* read serialized length and ack_id */
  char header[2 * sizeof(uint32_t)];
  auto ret = fb_read(fd, header, sizeof(header));
  if (ret == -1 || ret == 0) {
    return ret;
  }
  msg_size = reinterpret_cast<uint32_t *>(header)[0];
  if (ack_id_p) {
    *ack_id_p = reinterpret_cast<uint32_t *>(header)[1];
  }

  /* read serialized msg */
  *bufp = new char[msg_size];
  if ((ret = fb_read(fd, *bufp, msg_size)) == -1) {
    delete[] *bufp;
    return ret;
  }

  return ret;
}

/**
 * Checks if a path semantically begins with the given subpath.
 *
 * Does string operations only, does not look at the file system.
 */
bool path_begins_with(const std::string& path, const std::string& prefix) {
  /* Strip off trailing slashes from prefix. */
  auto prefixlen = prefix.length();
  while (prefixlen > 0 && prefix[prefixlen - 1] == '/') {
    prefixlen--;
  }

  if (path.length() < prefixlen) {
    return false;
  }

  if (memcmp(path.c_str(), prefix.c_str(), prefixlen) != 0) {
    return false;
  }

  if (path.length() == prefixlen) {
    return true;
  }

  if (path.c_str()[prefix.length()] == '/') {
    return true;
  }

  return false;
}

/**
 * ACK a message from the supervised process
 * @param conn connection file descriptor to send the ACK on
 * @param ack_num the ACK id
 */
void ack_msg(const int conn, const int ack_num) {
  FB_DEBUG(firebuild::FB_DEBUG_COMM, "sending ACK no. " + std::to_string(ack_num));
  fbb_send(conn, NULL, ack_num);
  FB_DEBUG(firebuild::FB_DEBUG_COMM, "ACK sent");
}

}  // namespace firebuild
