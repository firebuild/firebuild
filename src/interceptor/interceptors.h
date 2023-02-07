/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 *
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 * Modification and redistribution are permitted, but commercial use of derivative
 * works is subject to the same requirements of this license
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

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
#ifdef __APPLE__
#include <mach/error.h>
#include <mach-o/dyld.h>
#include <netdb.h>
#else
#include <error.h>
#endif
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#ifdef __APPLE__
#include <sys/mman.h>
#include <sys/random.h>
#endif
#include <sys/resource.h>
#include <sys/socket.h>
#ifdef __APPLE__
#include <sys/shm.h>
#endif
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/timeb.h>
#ifdef __linux__
#include <bits/timex.h>
#endif
// #include <sys/timex.h>
struct ntptimeval;
#include <sys/types.h>
#include <sys/uio.h>
#ifdef __linux__
#include <sys/vfs.h>
#endif
#include <sys/wait.h>
#include <unistd.h>
// #include <ustat.h>
struct statx;
struct ustat;
#include <utime.h>
#include <wchar.h>

#include "./fbbcomm.h"
#include "common/platform.h"

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

void reset_interceptors();

/* Include the auto-generated declarations of the get_ic_orig function pointers,
 * and some convenience #define redirects */
#include "interceptor/gen_decl.h"

#endif  // FIREBUILD_INTERCEPTORS_H_
