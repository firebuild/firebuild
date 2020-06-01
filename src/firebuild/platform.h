/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_PLATFORM_H_
#define FIREBUILD_PLATFORM_H_

#include <string>

namespace firebuild {

namespace platform {

inline bool path_is_absolute(const std::string &p) {
#ifdef _WIN32
  return !PathIsRelative(p);
#else
  if ((p.length() >= 1) && (p.at(0) == '/')) {
    return true;
  } else {
    return false;
  }
#endif
}

}  // namespace platform
}  // namespace firebuild
#endif  // FIREBUILD_PLATFORM_H_
