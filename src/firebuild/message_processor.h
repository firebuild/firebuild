/* Copyright (c) 2022 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_MESSAGE_PROCESSOR_H_
#define FIREBUILD_MESSAGE_PROCESSOR_H_

#include "firebuild/execed_process.h"
#include "firebuild/epoll.h"

namespace firebuild {

/** Handles incoming FBB messages from the interceptor */
class MessageProcessor {
 public:
  static void accept_exec_child(ExecedProcess* proc, int fd_conn, int fd0_reopen = -1);
  static void ic_conn_readcb(const struct epoll_event* event, void *ctx);
};

}  /* namespace firebuild */
#endif  // FIREBUILD_MESSAGE_PROCESSOR_H_
