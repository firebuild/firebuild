/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include "firebuild/utils.h"

#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <cstdlib>
#include <vector>

#include "./fbbcomm.h"
#include "common/firebuild_common.h"
#include "firebuild/debug.h"

/** wrapper for writev() retrying on recoverable errors (EINTR and short write) */
ssize_t fb_write(int fd, const void *buf, size_t count) {
  FB_READ_WRITE(write, fd, buf, count);
}

/** wrapper for writev() retrying on recoverable errors (EINTR and short write) */
ssize_t fb_writev(int fd, struct iovec *iov, int iovcnt) {
  FB_READV_WRITEV(writev, fd, iov, iovcnt);
}

/** Wrapper retrying on recoverable errors (short copy) */
ssize_t fb_copy_file_range(int fd_in, loff_t *off_in, int fd_out, loff_t *off_out, size_t len,
                           unsigned int flags) {
  ssize_t ret;
  size_t remaining = len;
  do {
    ret = copy_file_range(fd_in, off_in, fd_out, off_out, remaining, flags);
    if (ret == -1) {
      if (errno == EXDEV) {
        firebuild::fb_perror("copy_file_range");
        // TODO(rbalint) fall back to fb_read and fb_write
        assert(0 && "cache and system or build area on different mount points is supported only "
               "with Linux 5.3 and later");
      } else {
        return ret;
      }
    } else if (ret == 0) {
      return len - remaining;
    } else {
      remaining -= ret;
    }
  } while (remaining > 0);
  return len;
}

bool get_fdinfo(pid_t pid, int fd, ssize_t *offset, int *flags) {
  char buf[64];
  snprintf(buf, sizeof(buf), "/proc/%d/fdinfo/%d", pid, fd);
  FILE *f = fopen(buf, "r");
  if (f == NULL) {
    return false;
  }
  bool offset_found = (offset == nullptr);
  bool flags_found = (flags == nullptr);
  ssize_t value;
  while (!(offset_found && flags_found) && fscanf(f, "%63s%li", buf, &value) == 2) {
    if (strcmp(buf, "pos:") == 0) {
      if (offset) {
        *offset = value;
      }
      offset_found = true;
    } else if (strcmp(buf, "flags:") == 0) {
      if (flags) {
        *flags = value;
      }
      flags_found = true;
    }
  }
  fclose(f);
  return offset_found && flags_found;
}

namespace firebuild {

/**
 * ACK a message from the supervised process
 * @param conn connection file descriptor to send the ACK on
 * @param ack_num the ACK id
 */
void ack_msg(const int conn, const uint16_t ack_num) {
  TRACK(FB_DEBUG_COMM, "conn=%s, ack_num=%d", D_FD(conn), ack_num);

  FB_DEBUG(firebuild::FB_DEBUG_COMM, "sending ACK no. " + d(ack_num));
  msg_header msg = {};
  msg.ack_id = ack_num;
  fb_write(conn, &msg, sizeof(msg));
  FB_DEBUG(firebuild::FB_DEBUG_COMM, "ACK sent");
}

/**
 * Send an FBB message along with its header, potentially attaching two fds as ancillary data.
 *
 * These fds will appear in the intercepted process as opened file descriptors, possibly at
 * different numeric values (the numbers are automatically rewritten by the kernel).
 * This is sort of a cross-process dup(), see SCM_RIGHTS in cmsg(3) and unix(7).
 * Also see #656 for the overall design why we're doing this.
 *
 * If there are fds to attach, the message header and the message payload are sent in separate
 * steps, the message payload carrying the attached fds.
 *
 * @param conn connection file descriptor
 * @param ack_num the ack_num to send
 * @param msg the FBB message's builder object
 * @param fds pointer to the file descriptor array
 * @param fd_count number of fds to send
 */
void send_fbb(int conn, int ack_num, const FBBCOMM_Builder *msg, int *fds, int fd_count) {
  TRACK(FB_DEBUG_COMM, "conn=%s, ack_num=%d fd_count=%d", D_FD(conn), ack_num, fd_count);

  if (FB_DEBUGGING(firebuild::FB_DEBUG_COMM)) {
    std::vector<int> fds_vec(fds, fds + fd_count);
    fprintf(stderr, "Sending message with ancillary fds %s:\n", D(fds_vec));
    msg->debug(stderr);
  }

  int len = msg->measure();

  char *buf = reinterpret_cast<char *>(alloca(sizeof(msg_header) + len));
  memset(buf, 0, sizeof(msg_header));
  reinterpret_cast<msg_header *>(buf)->ack_id = ack_num;
  reinterpret_cast<msg_header *>(buf)->msg_size = len;
  reinterpret_cast<msg_header *>(buf)->fd_count = fd_count;

  msg->serialize(buf + sizeof(msg_header));

  if (fd_count == 0) {
    /* No fds to attach. Send the header and the payload in a single step. */
    fb_write(conn, buf, sizeof(msg_header) + len);
  } else {
    /* We have some fds to attach. Send the header and the payload separately. This means that the
     * file descriptors (ancillary data) are attached to the first byte of the payload. */

    /* Send the header. */
    fb_write(conn, buf, sizeof(msg_header));

    /* Prepare to send the payload, with the fds attached as ancillary data. */
    struct iovec iov = {};
    iov.iov_base = buf + sizeof(msg_header);
    iov.iov_len = len;

    void *anc_buf;
    size_t anc_buf_size = CMSG_SPACE(fd_count * sizeof(int));
    anc_buf = alloca(anc_buf_size);
    memset(anc_buf, 0, anc_buf_size);

    struct msghdr msgh = {};
    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;
    msgh.msg_control = anc_buf;
    msgh.msg_controllen = anc_buf_size;

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msgh);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(fd_count * sizeof(int));
    memcpy(CMSG_DATA(cmsg), fds, fd_count * sizeof(int));

    /* Send the payload. The socket is almost empty (it can only contain the header), so we can
     * safely expect sendmsg() to fully succeed, no short write, if the message is reasonably sized.
     * FIXME implement fb_sendmsg() which retries, just to be even safer. */
    sendmsg(conn, &msgh, 0);
  }
}

void fb_perror(const char *s) {
  perror((std::string("FIREBUILD: ") + s).c_str());
}
}  /* namespace firebuild */
