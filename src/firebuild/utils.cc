/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include "firebuild/utils.h"

#include <string>
#include <cstdlib>

#include "./fbb.h"
#include "common/firebuild_common.h"
#include "firebuild/debug.h"

/** wrapper for writev() retrying on recoverable errors */
ssize_t fb_write(int fd, const void *buf, size_t count) {
  FB_READ_WRITE(write, fd, buf, count);
}

/** wrapper for writev() retrying on recoverable errors */
ssize_t fb_writev(int fd, struct iovec *iov, int iovcnt) {
  FB_READV_WRITEV(writev, fd, iov, iovcnt);
}

namespace firebuild {

/**
 * ACK a message from the supervised process
 * @param conn connection file descriptor to send the ACK on
 * @param ack_num the ACK id
 */
void ack_msg(const FD conn, const int ack_num) {
  TRACK(FB_DEBUG_COMM, "conn=%s, ack_num=%d", D(conn), ack_num);

  FB_DEBUG(firebuild::FB_DEBUG_COMM, "sending ACK no. " + d(ack_num));
  /* This method is often used to send a delayed ack, where in some rare cases the underlying fd
   * might have been closed, or even reopened as another file. See #433. Handle this case gently,
   * by calling fd_safe() which returns -1 and then the write attempt silently fails. */
  fbb_send(conn.fd_safe(), NULL, ack_num);
  FB_DEBUG(firebuild::FB_DEBUG_COMM, "ACK sent");
}

}  // namespace firebuild
