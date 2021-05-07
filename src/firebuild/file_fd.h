/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_FILE_FD_H_
#define FIREBUILD_FILE_FD_H_

#include <fcntl.h>

#include <cassert>
#include <memory>
#include <string>

#include "firebuild/file.h"
#include "firebuild/file_name.h"
#include "firebuild/cxx_lang_utils.h"
#include "firebuild/pipe.h"

namespace firebuild {
#define FD_ORIGIN_ENUM {                                                \
    FD_ORIGIN_FILE_OPEN, /* backed by open()-ed file */                 \
    FD_ORIGIN_INTERNAL,  /* backed by memory (e.g. using fmemopen()) */ \
    FD_ORIGIN_PIPE,      /* pipe endpoint (e.g. using pipe()) */        \
    FD_ORIGIN_DUP,       /* created using dup() */                      \
    FD_ORIGIN_ROOT       /* inherited in the root process (stdin...) */ }

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
class Pipe;

class FileFD {
 public:
  /** Constructor for fds inherited from the supervisor (stdin, stdout, stderr). */
  FileFD(int fd, int flags)
      : fd_(fd), curr_flags_(flags), origin_type_(FD_ORIGIN_ROOT), close_on_popen_(false),
        read_(false), written_(false), open_(fd_ >= 0), origin_fd_(NULL),
        filename_(), pipe_(), opened_by_(NULL) {}
  /** Constructor for fds backed by internal memory. */
  FileFD(int fd, int flags, Process * const p)
      : fd_(fd), curr_flags_(flags), origin_type_(FD_ORIGIN_INTERNAL), close_on_popen_(false),
        read_(false), written_(false), open_(fd_ >= 0), origin_fd_(NULL),
        filename_(), pipe_(), opened_by_(p) {}
  /** Constructor for fds backed by a pipe including ones created by popen(). */
  FileFD(int fd, int flags, boost::local_shared_ptr<Pipe> pipe, Process * const p,
         bool close_on_popen = false)
      : fd_(fd), curr_flags_(flags), origin_type_(FD_ORIGIN_PIPE), close_on_popen_(close_on_popen),
        read_(false), written_(false), open_(fd_ >= 0), origin_fd_(NULL),
        filename_(), pipe_(pipe), opened_by_(p) {}
  /** Constructor for fds created from other fds through dup() or exec() */
  FileFD(int fd, int flags, fd_origin o, boost::local_shared_ptr<FileFD> o_fd)
      : fd_(fd), curr_flags_(flags), origin_type_(o), close_on_popen_(false),
        read_(false), written_(false), open_(fd_ >= 0), origin_fd_(o_fd),
        filename_(o_fd->filename()), pipe_(o_fd->pipe_),
        opened_by_(o_fd->opened_by()) {
    if (pipe_) {
      pipe_->handle_dup(o_fd.get(), this);
    }
  }
  /** Constructor for fds obtained through opening files. */
  FileFD(const FileName* f, int fd, int flags, Process * const p)
      : fd_(fd), curr_flags_(flags), origin_type_(FD_ORIGIN_FILE_OPEN), close_on_popen_(false),
        read_(false), written_(false), open_(true), origin_fd_(NULL),
        filename_(f), pipe_(), opened_by_(p) {}
  FileFD(FileFD&) = default;
  FileFD(const FileFD&) = default;
  FileFD& operator= (const FileFD&) = default;
  int fd() const {return fd_;}
  int last_err() {return last_err_;}
  void set_last_err(int err) {last_err_ = err;}
  int flags() const {return curr_flags_;}
  Process * opened_by() {return opened_by_;}
  bool open() {return open_;}
  void set_open(bool o) {open_ = o;}
  bool cloexec() const {return curr_flags_ & O_CLOEXEC;}
  void set_cloexec(bool value) {
    if (value) {
      curr_flags_ |= O_CLOEXEC;
    } else {
      curr_flags_ &= ~O_CLOEXEC;
    }
  }
  fd_origin origin_type() {return origin_type_;}
  bool close_on_popen() const {return close_on_popen_;}
  void set_close_on_popen(bool c) {close_on_popen_ = c;}
  bool read() {return read_;}
  bool written() {return written_;}
  const FileName* filename() const {return filename_;}
  void set_pipe(boost::local_shared_ptr<Pipe> pipe) {
    assert((origin_type_ == FD_ORIGIN_ROOT && !pipe_) || pipe_);
    if (pipe_) {
      pipe_->handle_close(this);
    }
    pipe_ = pipe;
  }
  boost::local_shared_ptr<Pipe> pipe() {return pipe_;}
  const boost::local_shared_ptr<Pipe> pipe() const {return pipe_;}

 private:
  int fd_;
  int curr_flags_;
  int last_err_ = 0;
  fd_origin origin_type_ : 3;
  bool close_on_popen_ : 1;
  bool read_ : 1;
  bool written_ : 1;
  /** file descriptor is open (valid) */
  bool open_ : 1;
  boost::local_shared_ptr<FileFD> origin_fd_;
  const FileName* filename_;
  boost::local_shared_ptr<Pipe> pipe_;
  /** Process that opened this file by name.
   *  Remains the same (doesn't get updated to the current process) at dup2() or alike.
   *  NULL if the topmost intercepted process already inherited it from the supervisor. */
  Process* opened_by_;
};

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const FileFD& ffd, const int level = 0);
std::string d(const FileFD *ffd, const int level = 0);

}  // namespace firebuild
#endif  // FIREBUILD_FILE_FD_H_
