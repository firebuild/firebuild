/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_UTILS_H_
#define FIREBUILD_UTILS_H_

#include <string>

/** Wrapper retrying on recoverable errors */
ssize_t fb_copy_file_range(int fd_in, loff_t *off_in, int fd_out, loff_t *off_out, size_t len,
                           unsigned int flags);

namespace firebuild {

class Process;

void ack_msg(Process *proc);
void ack_msg(const int conn, const uint32_t ack_num);

std::string make_fifo(int fd, int flags, int pid, const char *fb_conn_string,
                      int *fifo_name_offset);

}  // namespace firebuild

#endif  // FIREBUILD_UTILS_H_
