/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_PLATFORM_H_
#define FIREBUILD_PLATFORM_H_

#include <fcntl.h>
#include <unistd.h>

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

/**
 * Check if fd1 and fd2 point to the same place. kcmp() is not universally
 * available, so do a back-n-forth fcntl() on one and see if it drags the other with it.
 * See https://unix.stackexchange.com/questions/191967.
 * @return 0 if they point to the same place, 1 if they don't
 */
inline int fdcmp(int fd1, int fd2) {
  // FIXME With shim support we can't safely toggle the fcntl flags as it might affect other
  // processes. Use kcmp() or /proc instead.
  int flags1 = fcntl(fd1, F_GETFL);
  int flags2a = fcntl(fd2, F_GETFL);
  fcntl(fd1, F_SETFL, flags1 ^ O_NONBLOCK);
  int flags2b = fcntl(fd2, F_GETFL);
  fcntl(fd1, F_SETFL, flags1);
  return (flags2a != flags2b) ? 0 : 1;
}

}  // namespace platform
}  // namespace firebuild

#endif  // FIREBUILD_PLATFORM_H_
