/* Copyright (c) 2021 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include "firebuild/fd.h"

#include <string>

namespace firebuild {

FD FD::open(int fd) {
  TRACK(FB_DEBUG_FD, "fd=%d", fd);

  ensure_fd_in_array(fd);
  assert(!fd_to_age_[fd].opened);
  fd_to_age_[fd].seq++;
  fd_to_age_[fd].opened = true;
  return FD(fd, fd_to_age_[fd].seq);
}

void FD::close() {
  TRACK(FB_DEBUG_FD, "this=%s", D(this));

  assert(is_valid());
  fd_to_age_[fd_].opened = false;
}

/* Member debugging method. Not to be called directly, call the global d(obj_or_ptr) instead.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string FD::d_internal(const int level) const {
  (void)level;  /* unused */
  return d(fd_) + "." + d(seq_) + (is_valid() ? "" : "-OUTDATED");
}

std::vector<FDAge> FD::fd_to_age_;


/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const FD& fd, const int level) {
  return fd.d_internal(level);
}
std::string d(const FD *fd, const int level) {
  if (fd) {
    return d(*fd, level);
  } else {
    return "[FD NULL]";
  }
}

}  // namespace firebuild
