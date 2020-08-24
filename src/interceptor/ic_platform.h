/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_IC_PLATFORM_H_
#define FIREBUILD_IC_PLATFORM_H_

#include <assert.h>

#define FB_MISSING(thing) assert(0 && "Missing" && thing)

#ifdef __clang__
inline void* __builtin_apply_args() {
  FB_MISSING(__func__);
  return NULL;
}

inline void* __builtin_apply(void (*function)(), void * arguments, size_t size) {
  FB_MISSING(__func__);
  return NULL;
}

#endif  // __clang__

#endif  // FIREBUILD_IC_PLATFORM_H_
