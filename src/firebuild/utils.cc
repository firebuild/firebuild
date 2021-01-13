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
 * ACK a message from the supervised process
 * @param conn connection file descriptor to send the ACK on
 * @param ack_num the ACK id
 */
void ack_msg(const int conn, const int ack_num) {
  TRACK(FB_DEBUG_COMM, "conn=%d, ack_num=%d", conn, ack_num);

  FB_DEBUG(firebuild::FB_DEBUG_COMM, "sending ACK no. " + d(ack_num));
  fbb_send(conn, NULL, ack_num);
  FB_DEBUG(firebuild::FB_DEBUG_COMM, "ACK sent");
}

}  // namespace firebuild
