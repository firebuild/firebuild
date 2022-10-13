/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

/* interceptors.{cc,h} are the minimum necessary boilerplate
 * around the auto-generated gen_* interceptor files. */

#ifndef FIREBUILD_INTERCEPTORS_H_
#define FIREBUILD_INTERCEPTORS_H_

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <dirent.h>
#include <dlfcn.h>
#include <err.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/timeb.h>
#include <bits/timex.h>
// #include <sys/timex.h>
struct ntptimeval;
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <unistd.h>
// #include <ustat.h>
struct statx;
struct ustat;
#include <utime.h>
#include <wchar.h>

#include "./fbbcomm.h"

#ifndef __GLIBC_PREREQ
#define FB_SSIZE_T ssize_t
#define FB_VA_LIST va_list
#else
#if __GLIBC_PREREQ (2, 28)
#define FB_SSIZE_T ssize_t
#define FB_VA_LIST va_list
#else
#define FB_SSIZE_T _IO_ssize_t
#define FB_VA_LIST _G_va_list
#endif
#endif

void init_interceptors();
void reset_interceptors();

/* Include the auto-generated declarations of the ic_orig function pointers,
 * and some convenience #define redirects */
#include "interceptor/gen_decl.h"

#endif  // FIREBUILD_INTERCEPTORS_H_
