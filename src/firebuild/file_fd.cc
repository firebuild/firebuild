/* Copyright (c) 2021 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include "firebuild/file_fd.h"

#include <string>

#include "firebuild/pipe.h"

namespace firebuild {

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const FileFD& ffd, const int level) {
  (void)level;  /* unused */
  std::string ret = "{FileFD fd=" + d(ffd.fd()) + " ";
  switch (ffd.flags() & O_ACCMODE) {
    case O_RDONLY:
      ret += "r";
      break;
    case O_WRONLY:
      ret += "w";
      break;
    case O_RDWR:
      ret += "rw";
      break;
    default:
      ret += "unknown_mode";
  }
  if (ffd.pipe()) {
    ret += " " + d(ffd.pipe().get(), level + 1);
  }
  if (ffd.filename()) {
    ret += " " + d(ffd.filename(), level + 1);
  }
  ret += "}";
  return ret;
}
std::string d(const FileFD *ffd, const int level) {
  if (ffd) {
    return d(*ffd, level);
  } else {
    return "{FileFD NULL}";
  }
}

}  // namespace firebuild
