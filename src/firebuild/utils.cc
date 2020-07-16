/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include <string>

#include "firebuild/utils.h"

#include "common/firebuild_common.h"
#include "firebuild/debug.h"

namespace firebuild {

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
  firebuild::msg::SupervisorMsg sv_msg;
  sv_msg.set_ack_num(ack_num);
  FB_DEBUG(firebuild::FB_DEBUG_COMM, "sending ACK no. " + std::to_string(ack_num));
  fb_send_msg(sv_msg, conn);
  FB_DEBUG(firebuild::FB_DEBUG_COMM, "ACK sent");
}

}  // namespace firebuild
