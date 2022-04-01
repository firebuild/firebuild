/* Copyright (c) 2022 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef COMMON_DEBUG_SYSFLAGS_H_
#define COMMON_DEBUG_SYSFLAGS_H_

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void debug_open_flags(FILE *f, int flags);
void debug_at_flags(FILE *f, int flags);
void debug_fcntl_cmd(FILE *f, int cmd);
void debug_fcntl_arg_or_ret(FILE *f, int cmd, int arg);
void debug_error_no(FILE *f, int error_no);
void debug_mode_t(FILE *f, mode_t mode);
void debug_wstatus(FILE *f, int wstatus);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* COMMON_DEBUG_SYSFLAGS_H_ */
