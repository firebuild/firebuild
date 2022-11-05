/* Copyright (c) 2022 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_SIGCHILD_CALLBACK_H_
#define FIREBUILD_SIGCHILD_CALLBACK_H_

#include "firebuild/epoll.h"

namespace firebuild {

void sigchild_cb(const struct epoll_event* event, void *arg);

}  /* namespace firebuild */
#endif  // FIREBUILD_SIGCHILD_CALLBACK_H_
