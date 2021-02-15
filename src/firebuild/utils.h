/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_UTILS_H_
#define FIREBUILD_UTILS_H_

#include <string>

namespace firebuild {

typedef struct msg_header_ {
  uint32_t msg_size;
  uint32_t ack_id;
} msg_header;

void ack_msg(const int conn, const int ack_num);

}  // namespace firebuild
#endif  // FIREBUILD_UTILS_H_
