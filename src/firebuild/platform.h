/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_PLATFORM_H_
#define FIREBUILD_PLATFORM_H_

#include <fcntl.h>
#ifdef __linux__
#include <linux/kcmp.h>
#include <sys/syscall.h>
#endif
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
 * Check if fd1 and fd2 point to the same place.
 * kcmp() is not universally available, so in its absence do a back-n-forth fcntl() on one and see
 * if it drags the other with it.
 * See https://unix.stackexchange.com/questions/191967.
 * @return 0 if they point to the same place, -1 or 1 if fd1 sorts lower or higher than fd2 in an
 * arbitrary ordering to help using fdcmp for sorting
 */
inline int fdcmp(int fd1, int fd2) {
#ifdef __linux__
  pid_t pid = getpid();
  switch (syscall(SYS_kcmp, pid, pid, KCMP_FILE, fd1, fd2)) {
    case 0: return 0;
    case 1: return -1;
    case 2: return 1;
    case 3: return fd1 < fd2 ? -1 : 1;
    case -1: {
#endif
      /* TODO(rbalint) this may not be safe for shim, but I have no better idea */
      int flags1 = fcntl(fd1, F_GETFL);
      int flags2a = fcntl(fd2, F_GETFL);
      fcntl(fd1, F_SETFL, flags1 ^ O_NONBLOCK);
      int flags2b = fcntl(fd2, F_GETFL);
      fcntl(fd1, F_SETFL, flags1);
      return (flags2a != flags2b) ? 0 : (fd1 < fd2 ? -1 : 1);
#ifdef __linux__
    }
    default:
      assert(0 && "not reached");
      abort();
  }
#endif
}

}  /* namespace platform */
}  /* namespace firebuild */

#endif  // FIREBUILD_PLATFORM_H_
