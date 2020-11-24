/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_FILE_FD_H_
#define FIREBUILD_FILE_FD_H_

#include <fcntl.h>

#include <memory>
#include <string>

#include "firebuild/file.h"
#include "firebuild/file_name.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild {
#define FD_ORIGIN_ENUM {                                                \
    FD_ORIGIN_FILE_OPEN, /* backed by open()-ed file */                 \
    FD_ORIGIN_INTERNAL,  /* backed by memory (e.g. using fmemopen()) */ \
    FD_ORIGIN_PIPE,      /* pipe endpoint (e.g. using pipe()) */        \
    FD_ORIGIN_DUP,       /* created using dup() */                      \
    FD_ORIGIN_ROOT       /* std fd of the root process (stdin, etc.) */ }

#ifdef __GNUC__
#  include <features.h>
#  if __GNUC_PREREQ(4, 8)
  enum fd_origin FD_ORIGIN_ENUM;
#  else
  // work around invalid conversion from unsigned char:2 to firebuild::fd_origin
  enum FD_ORIGIN_ENUM;
  typedef unsigned char fd_origin;
#  endif
#else
  //    If not gcc
  enum fd_origin FD_ORIGIN_ENUM;
#endif

class Process;

class FileFD {
 public:
  /** Constructor for fds inherited from the supervisor (stdin, stdout, stderr). */
  FileFD(int fd, int flags)
      : fd_(fd), curr_flags_(flags), origin_type_(FD_ORIGIN_ROOT), read_(false),
      written_(false), open_(fd_ >= 0), origin_fd_(NULL),
      filename_(), opened_by_(NULL) {}
  /** Constructor for fds backed by internal memory or a pipe. */
  FileFD(int fd, int flags, fd_origin origin_type, Process * const p)
      : fd_(fd), curr_flags_(flags), origin_type_(origin_type), read_(false),
      written_(false), open_(fd_ >= 0), origin_fd_(NULL),
      filename_(), opened_by_(p) {}
  /** Constructor for fds created from other fds through dup() or exec() */
  FileFD(int fd, int flags, fd_origin o, std::shared_ptr<FileFD> o_fd)
      : fd_(fd), curr_flags_(flags), origin_type_(o), read_(false),
      written_(false), open_(fd_ >= 0), origin_fd_(o_fd),
      filename_(), opened_by_(o_fd->opened_by()) {}
  /** Constructor for fds obtained through opening files. */
  FileFD(const FileName* f, int fd, int flags, Process * const p)
      : fd_(fd), curr_flags_(flags), origin_type_(FD_ORIGIN_FILE_OPEN),
      read_(false), written_(false), open_(true), origin_fd_(NULL),
      filename_(f), opened_by_(p) {}
  FileFD(FileFD&) = default;
  FileFD& operator= (const FileFD&) = default;
  int last_err() {return last_err_;}
  void set_last_err(int err) {last_err_ = err;}
  int flags() {return curr_flags_;}
  Process * opened_by() {return opened_by_;}
  bool open() {return open_;}
  void set_open(bool o) {open_ = o;}
  bool cloexec() {return curr_flags_ & O_CLOEXEC;}
  void set_cloexec(bool value) {
    if (value) {
      curr_flags_ |= O_CLOEXEC;
    } else {
      curr_flags_ &= ~O_CLOEXEC;
    }
  }
  fd_origin origin_type() {return origin_type_;}
  bool read() {return read_;}
  bool written() {return written_;}
  const FileName* filename() {return filename_;}

 private:
  int fd_;
  int curr_flags_;
  int last_err_ = 0;
  fd_origin origin_type_ : 3;
  bool read_ : 1;
  bool written_ : 1;
  /** file descriptor is open (valid) */
  bool open_ : 1;
  std::shared_ptr<FileFD> origin_fd_;
  const FileName* filename_;
  /** Process that opened this file by name.
   *  Remains the same (doesn't get updated to the current process) at dup2() or alike.
   *  NULL if the topmost intercepted process already inherited it from the supervisor. */
  Process* opened_by_;
};

}  // namespace firebuild
#endif  // FIREBUILD_FILE_FD_H_
