/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 */

#ifndef FIREBUILD_FIREBUILD_H_
#define FIREBUILD_FIREBUILD_H_

extern int child_pid, child_ret;

extern int sigchild_selfpipe[2];

extern int listener;

#endif  // FIREBUILD_FIREBUILD_H_
