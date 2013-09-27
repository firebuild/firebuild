
#ifndef FIREBUILD_COMMON_H
#define FIREBUILD_COMMON_H

#include <google/protobuf/message_lite.h>

ssize_t fb_send_msg (google::protobuf::MessageLite &pb_msg, int fd);
ssize_t fb_recv_msg (google::protobuf::MessageLite &pb_msg, int fd);

/**
 * wrapper for write() retrying on recoverable errors
 *
 * It is implemented differently in supervisor and interceptor
 */
ssize_t fb_write_buf(int fd, const void *buf, size_t count);

/**
 * wrapper for read() retrying on recoverable errors
 *
 * It is implemented differently in supervisor and interceptor
 */
ssize_t fb_read_buf(int fd, const void *buf, size_t count);

/** wrapper macro for write() or read() retrying on recoverable errors */
#define FB_IO_OP_BUF(mp_op, mp_fd, mp_buf, mp_count, mp_cleanup_block)	\
  {                                                                     \
    ssize_t op_ret;                                                     \
    char * buf_pt = static_cast<char*>(const_cast<void*>(mp_buf));      \
    size_t remaining = mp_count;                                        \
    do {                                                                \
      op_ret = mp_op(mp_fd, buf_pt, remaining);                         \
      if (op_ret == -1) {                                               \
        if(errno == EINTR){                                             \
          continue;                                                     \
        } else {                                                        \
          mp_cleanup_block;                                             \
          return op_ret;                                                \
        }                                                               \
      } else if (op_ret == 0) {                                         \
        mp_cleanup_block;                                               \
        return op_ret;                                                  \
      } else {                                                          \
        remaining -= op_ret;                                            \
        buf_pt += op_ret;                                               \
      }                                                                 \
    } while (remaining > 0);                                            \
    mp_cleanup_block;                                                   \
    return mp_count;                                                    \
  }

#endif
