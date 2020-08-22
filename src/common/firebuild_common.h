/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#ifndef COMMON_FIREBUILD_COMMON_H_
#define COMMON_FIREBUILD_COMMON_H_

#include <limits.h>
#include <sys/uio.h>

#include <google/protobuf/message_lite.h>

namespace firebuild {

ssize_t fb_send_msg_unlocked(const google::protobuf::MessageLite &pb_msg, const int fd);
ssize_t fb_recv_msg_unlocked(google::protobuf::MessageLite *pb_msg, const int fd);

}  /* namespace firebuild */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * wrapper for write() retrying on recoverable errors
 *
 * It is implemented differently in supervisor and interceptor
 */
ssize_t fb_write(int fd, const void *buf, size_t count);

/**
 * wrapper for read() retrying on recoverable errors
 *
 * It is implemented differently in supervisor and interceptor
 */
ssize_t fb_read(int fd, void *buf, size_t count);

#ifdef __cplusplus
}  /* extern "C" */
#endif

/** Wrapper macro for read() or write() retrying on recoverable errors */
#define FB_READ_WRITE(op, fd, buf, count)                               \
  {                                                                     \
    ssize_t ret;                                                        \
    size_t remaining = count;                                           \
    do {                                                                \
      ret = (op)(fd, buf, remaining);                                   \
      if (ret == -1) {                                                  \
        if (errno == EINTR) {                                           \
          continue;                                                     \
        } else {                                                        \
          return ret;                                                   \
        }                                                               \
      } else if (ret == 0) {                                            \
        return ret;                                                     \
      } else {                                                          \
        remaining -= ret;                                               \
        buf = ((char *) buf) + ret;                                     \
      }                                                                 \
    } while (remaining > 0);                                            \
    return count;                                                       \
  }

/**
 * Wrapper macro for readv() or writev(), with the following differences:
 *
 * - retries/continues on recoverable errors (EINTR and short write);
 *
 * - iov/iovcnt can be arbitrarily large (if it's larger than IOV_MAX then
 *   the operation will not be atomic though);
 *
 * - in order to implement the previous two without having to copy the
 *   entire iov array, iov isn't const.
 */
#define FB_READV_WRITEV(op, fd, iov, iovcnt)                            \
  {                                                                     \
    ssize_t ret;                                                        \
    ssize_t written = 0;                                                \
    ssize_t remaining = 0;                                              \
    for (int i = 0; i < iovcnt; i++) {                                  \
      remaining += iov[i].iov_len;                                      \
    }                                                                   \
    while (1) {                                                         \
      ret = (op)(fd, iov, iovcnt <= IOV_MAX ? iovcnt : IOV_MAX);        \
      if (ret == remaining) {                                           \
        /* completed */                                                 \
        return written + ret;                                           \
      } else if (ret == -1) {                                           \
        if (errno == EINTR) {                                           \
          /* retry on signal */                                         \
          continue;                                                     \
        } else {                                                        \
          /* bail out on permanent error */                             \
          return ret;                                                   \
        }                                                               \
      } else {                                                          \
        /* some, but not all bytes have been written */                 \
        written += ret;                                                 \
        remaining -= ret;                                               \
        while ((size_t) ret >= iov[0].iov_len) {                        \
          /* skip the fully written chunks */                           \
          ret -= iov[0].iov_len;                                        \
          iov++;                                                        \
          iovcnt--;                                                     \
        }                                                               \
        /* adjust the partially written chunk */                        \
        iov[0].iov_base = ((char *) iov[0].iov_base) + ret;             \
        iov[0].iov_len -= ret;                                          \
      }                                                                 \
    }                                                                   \
  }

#endif  /* COMMON_FIREBUILD_COMMON_H_ */
