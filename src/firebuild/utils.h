/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_UTILS_H_
#define FIREBUILD_UTILS_H_

#include <string>

/** Wrapper retrying on recoverable errors */
ssize_t fb_copy_file_range(int fd_in, loff_t *off_in, int fd_out, loff_t *off_out, size_t len,
                           unsigned int flags);

/** Subtract two struct timespecs. Equivalent to the same method of BSD. */
#define timespecsub(a, b, res) do {             \
  (res)->tv_sec = (a)->tv_sec - (b)->tv_sec;    \
  (res)->tv_nsec = (a)->tv_nsec - (b)->tv_nsec; \
  if ((res)->tv_nsec < 0) {                     \
    (res)->tv_sec--;                            \
    (res)->tv_nsec += 1000 * 1000 * 1000;       \
  }                                             \
} while (0)

namespace firebuild {

void ack_msg(const int conn, const uint32_t ack_num);

std::string make_fifo(int fd, int flags, int pid, const char *fb_conn_string,
                      int *fifo_name_offset);

}  // namespace firebuild
#endif  // FIREBUILD_UTILS_H_
