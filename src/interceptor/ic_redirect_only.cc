/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

/* Exported functions calling other functions directly without dlsym lookup
 * tricks */

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <cstdarg>
#include <cstdlib>

#ifdef  __cplusplus
extern "C" {
#endif

/* make redirected functions visible */
#pragma GCC visibility push(default)

/**
 * vfork simply calling fork
 *
 * vfork interception would be a bit complicated to implement properly
 * and most of the programs will work properly with fork
 */
extern pid_t vfork(void) {
  return fork();
}

/**
 * creat calling equivalent open
 */
extern int creat(const char *pathname, mode_t mode) {
  return open(pathname, (O_CREAT|O_WRONLY|O_TRUNC), mode);
}

/**
 * creat64 calling equivalent open64
 */
extern int creat64(const char *pathname, mode_t mode) {
  return open64(pathname, (O_CREAT|O_WRONLY|O_TRUNC), mode);
}

/**
 * eaccess() is a synonym for euidaccess()
 */
extern int eaccess(const char *pathname, int mode) {
  return euidaccess(pathname, mode);
}

#pragma GCC visibility pop

#ifdef  __cplusplus
}  // extern "C"
#endif
