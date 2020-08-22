/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_UTILS_H_
#define FIREBUILD_UTILS_H_

#include <string>

namespace firebuild {

ssize_t fb_recv_msg(char **bufp, uint32_t *ack_id_p, int fd);

bool path_begins_with(const std::string& path, const std::string& prefix);

void ack_msg(const int conn, const int ack_num);

}  // namespace firebuild
#endif  // FIREBUILD_UTILS_H_
