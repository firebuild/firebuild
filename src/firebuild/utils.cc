/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include "firebuild/utils.h"

#include <sys/types.h>
#include <sys/stat.h>

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

/** Wrapper retrying on recoverable errors */
ssize_t fb_copy_file_range(int fd_in, loff_t *off_in, int fd_out, loff_t *off_out, size_t len,
                           unsigned int flags) {
  ssize_t ret;
  size_t remaining = len;
  do {
    ret = copy_file_range(fd_in, off_in, fd_out, off_out, remaining, flags);
    if (ret == -1) {
      if (errno == EXDEV) {
        perror("copy_file_range");
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

namespace firebuild {

/**
 * ACK a message from the supervised process
 * @param conn connection file descriptor to send the ACK on
 * @param ack_num the ACK id
 */
void ack_msg(const int conn, const int ack_num) {
  TRACK(FB_DEBUG_COMM, "conn=%s, ack_num=%d", D_FD(conn), ack_num);

  FB_DEBUG(firebuild::FB_DEBUG_COMM, "sending ACK no. " + d(ack_num));
  fbb_send(conn, NULL, ack_num);
  FB_DEBUG(firebuild::FB_DEBUG_COMM, "ACK sent");
}

char* make_fifo(int fd, int flags, int pid, const char *fb_conn_string, int *fifo_name_offset) {
  struct timespec time;
  clock_gettime(CLOCK_REALTIME, &time);
  char* fifo_params, *fifo;
  if (asprintf(&fifo_params, "%d:%d %n%s-%d-%d-%09ld-%09ld",
               fd, flags, fifo_name_offset, fb_conn_string, pid, fd,
               time.tv_sec, time.tv_nsec) == -1) {
    perror("asprintf");
    return nullptr;
  }
  fifo = fifo_params + *fifo_name_offset;
  int ret = mkfifo(fifo, 0666);
  if (ret == -1) {
    perror("could not create fifo");
    return nullptr;
  }
  return fifo_params;
}

}  // namespace firebuild
