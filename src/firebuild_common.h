/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#ifndef FIREBUILD_FIREBUILD_COMMON_H
#define FIREBUILD_FIREBUILD_COMMON_H

#include <google/protobuf/message_lite.h>

namespace firebuild {

ssize_t fb_send_msg(const google::protobuf::MessageLite &pb_msg, const int fd);
ssize_t fb_recv_msg(google::protobuf::MessageLite *pb_msg, const int fd);

/**
 * wrapper for write() retrying on recoverable errors
 *
 * It is implemented differently in supervisor and interceptor
 */
ssize_t fb_write_buf(const int fd, const void * const buf, const size_t count);

/**
 * wrapper for read() retrying on recoverable errors
 *
 * It is implemented differently in supervisor and interceptor
 */
ssize_t fb_read_buf(const int fd, void * buf, const size_t count);

/** wrapper macro for send() or recv() retrying on recoverable errors */
#define FB_IO_OP_BUF(mp_op, mp_fd, mp_buf, mp_count, mp_flags, mp_cleanup_bl) \
  {                                                                     \
    ssize_t op_ret;                                                     \
    char * buf_pt = static_cast<char*>(const_cast<void*>(mp_buf));      \
    size_t remaining = mp_count;                                        \
    do {                                                                \
      op_ret = mp_op(mp_fd, buf_pt, remaining, mp_flags);               \
      if (op_ret == -1) {                                               \
        if (errno == EINTR) {                                           \
          continue;                                                     \
        } else {                                                        \
          mp_cleanup_bl;                                                \
          return op_ret;                                                \
        }                                                               \
      } else if (op_ret == 0) {                                         \
        mp_cleanup_bl;                                                  \
        return op_ret;                                                  \
      } else {                                                          \
        remaining -= op_ret;                                            \
        buf_pt += op_ret;                                               \
      }                                                                 \
    } while (remaining > 0);                                            \
    mp_cleanup_bl;                                                      \
    return mp_count;                                                    \
  }

}  // namespace firebuild

#endif
