/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_FILEFD_H_
#define FIREBUILD_FILEFD_H_

#include <fcntl.h>

#include <string>

#include "firebuild/file.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild {
#define FD_ORIGIN_ENUM {                                                \
      FD_ORIGIN_FILE_OPEN, /* backed by open()-ed file */               \
      FD_ORIGIN_INTERNAL,  /* backed by memory (e.g. using pipe()) */   \
      FD_ORIGIN_DUP,       /* created using dup() */                    \
      FD_ORIGIN_INHERITED, /* inherited through fork() or exec() */     \
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
  /** Constructor for fds backed by internal memory, like pipe() created fds. */
  FileFD(int fd, int flags, Process * const p)
      : fd_(fd), curr_flags_(flags), origin_type_(FD_ORIGIN_INTERNAL),
      read_(false), written_(false), open_((fd_ >= 0)?true:false),
      origin_fd_(NULL) , filename_(), process_(p) {}
  /** Constructor for fds created from other fds through dup() or exec() */
  FileFD(int fd, int flags, fd_origin o, FileFD * o_fd, Process * const p)
      : fd_(fd), curr_flags_(flags), origin_type_(o), read_(false),
      written_(false), open_((fd_ >= 0)?true:false), origin_fd_(o_fd),
      filename_(), process_(p) {}
  /** Constructor for fds obtained through opening files. */
  FileFD(const std::string &f, int fd, int flags, Process * const p)
      :fd_(fd), curr_flags_(flags), origin_type_(FD_ORIGIN_FILE_OPEN),
      read_(false), written_(false), open_(true), origin_fd_(NULL),
      filename_(f), process_(p) {}
  int last_err() {return last_err_;}
  void set_last_err(int err) {last_err_ = err;}
  int flags() {return curr_flags_;}
  Process * process() {return process_;}
  bool open() {return open_;}
  void set_open(bool o) {open_ = o;}

  /**
   * Create new fd inherited from this one
   */
  FileFD* inherit(Process * const p) {
    return new FileFD (fd_, curr_flags_, FD_ORIGIN_INHERITED, this, p);
  }

  /**
   * Create new fd dup()-d from this one
   * @param fd fd number of result
   */
  FileFD* dup(const int fd, Process * const p) {
    return new FileFD (fd, curr_flags_, FD_ORIGIN_INHERITED, this, p);
  }

 private:
  int fd_;
  int curr_flags_;
  int last_err_ = 0;
  fd_origin origin_type_ : 3;
  bool read_ : 1;
  bool written_ : 1;
  /** file descriptor is open (valid) */
  bool open_ : 1;
  FileFD* origin_fd_;
  std::string filename_;
  Process* process_;
  /** Process the fd has been created in */
  DISALLOW_COPY_AND_ASSIGN(FileFD);
};

}  // namespace firebuild
#endif  // FIREBUILD_FILEFD_H_
