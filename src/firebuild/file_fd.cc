/* Copyright (c) 2021 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include "firebuild/file_fd.h"

#include <string>

#include "firebuild/pipe.h"

namespace firebuild {

int FileOFD::id_counter_ = 0;

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const FileOFD& fofd, const int level) {
  (void)level;  /* unused */
  std::string ret = "{FileOFD #" + d(fofd.id());
  ret += " type=" + std::string(fd_type_to_string(fofd.type())) + " ";
  // FIXME replace this with printing all the flags
  switch (fofd.flags() & O_ACCMODE) {
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
  if (fofd.filename()) {
    ret += " " + d(fofd.filename(), level + 1);
  }
  ret += "}";
  return ret;
}
std::string d(const FileOFD *fofd, const int level) {
  if (fofd) {
    return d(*fofd, level);
  } else {
    return "{FileOFD NULL}";
  }
}
std::string d(const FileFD& ffd, const int level) {
  std::string ret = "{FileFD fd=" + d(ffd.fd()) + " " + d(ffd.ofd(), level);
  if (ffd.pipe()) {
    ret += " " + d(ffd.pipe().get(), level + 1);
    ret += " close_on_popen=" + d(ffd.close_on_popen());
  }
  ret += " cloexec=" + d(ffd.cloexec());
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

const char *fd_type_to_string(fd_type type) {
  switch (type) {
    case FD_UNINITIALIZED:
      return "FD_UNINITIALIZED";
    case FD_IGNORED:
      return "FD_IGNORED";
    case FD_FILE:
      return "FD_FILE";
    case FD_PIPE_IN:
      return "FD_PIPE_IN";
    case FD_PIPE_OUT:
      return "FD_PIPE_OUT";
    case FD_SPECIAL:
      return "FD_SPECIAL";
    case FD_SCM_RIGHTS:
      return "FD_SCM_RIGHTS";
    default:
      assert(0 && "unknown type");
      return "UNKNOWN";
  }
}

}  /* namespace firebuild */
