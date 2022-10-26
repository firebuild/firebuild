/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 */

#ifndef FIREBUILD_SIGCHILD_CALLBACK_H_
#define FIREBUILD_SIGCHILD_CALLBACK_H_

#include "firebuild/epoll.h"

namespace firebuild {

void sigchild_cb(const struct epoll_event* event, void *arg);

}  /* namespace firebuild */
#endif  // FIREBUILD_SIGCHILD_CALLBACK_H_
