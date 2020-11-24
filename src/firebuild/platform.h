/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_PLATFORM_H_
#define FIREBUILD_PLATFORM_H_

#include <cstring>

namespace firebuild {

namespace platform {

inline bool path_is_absolute(const char * p) {
#ifdef _WIN32
  return !PathIsRelative(p);
#else
  if ((strlen(p) >= 1) && (p[0] == '/')) {
    return true;
  } else {
    return false;
  }
#endif
}

}  // namespace platform
}  // namespace firebuild
#endif  // FIREBUILD_PLATFORM_H_
