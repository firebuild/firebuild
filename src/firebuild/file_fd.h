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

class Process;
class Pipe;

class FileFD {
 public:
  /** Constructor for fds inherited from the supervisor (stdin, stdout, stderr). */
  FileFD(int fd, int flags)
      : fd_(fd), curr_flags_(flags), filename_(), pipe_(), opened_by_(NULL) {
    assert(fd >= 0);
  }
  /** Constructor for fds backed by internal memory. */
  FileFD(int fd, int flags, Process * const p)
      : fd_(fd), curr_flags_(flags), filename_(), pipe_(), opened_by_(p) {
    assert(fd >= 0);
  }
  /** Constructor for fds backed by a pipe including ones created by popen(). */
  FileFD(int fd, int flags, std::shared_ptr<Pipe> pipe, Process * const p,
         bool close_on_popen = false)
      : fd_(fd), curr_flags_(flags), close_on_popen_(close_on_popen),
        filename_(), pipe_(pipe), opened_by_(p) {
    assert(fd >= 0);
  }
  /** Constructor for fds created from other fds through dup() or exec() */
  FileFD(int fd, int flags, std::shared_ptr<FileFD> o_fd)
      : fd_(fd), curr_flags_(flags), filename_(o_fd->filename()), pipe_(o_fd->pipe_),
        opened_by_(o_fd->opened_by()) {
    assert(fd >= 0);
    if (filename_ && is_write(curr_flags_)) {
      filename_->open_for_writing(opened_by_);
    }
    if (pipe_) {
      pipe_->handle_dup(o_fd.get(), this);
    }
  }
  /** Constructor for fds obtained through opening files. */
  FileFD(const FileName* f, int fd, int flags, Process * const p)
      : fd_(fd), curr_flags_(flags), filename_(f), pipe_(), opened_by_(p) {
    assert(fd >= 0);
    if (is_write(curr_flags_)) {
      f->open_for_writing(opened_by_);
    }
  }
  FileFD(const FileFD& other)
      : fd_(other.fd_), curr_flags_(other.curr_flags_),
        close_on_popen_(other.close_on_popen_), read_(other.read_), written_(other.written_),
        filename_(other.filename_), pipe_(other.pipe_), opened_by_(other.opened_by_) {
    if (filename_ && is_write(curr_flags_)) {
      filename_->open_for_writing(opened_by_);
    }
  }
  FileFD& operator= (const FileFD& other) {
    fd_ = other.fd_;
    close_on_popen_ = other.close_on_popen_;
    read_ = other.read_;
    written_ = other.written_;
    if (filename_ != other.filename_) {
      if (filename_ && is_write(curr_flags_)) {
        filename_->close_for_writing();
      }
      filename_ = other.filename_;
      if (filename_ && is_write(other.curr_flags_)) {
        filename_->open_for_writing(opened_by_);
      }
    }
    curr_flags_ = other.curr_flags_;
    pipe_ = other.pipe_;
    opened_by_ = other.opened_by_;
    return *this;
  }
  ~FileFD() {
    if (is_write(curr_flags_) && filename_) {
      filename_->close_for_writing();
    }
  }
  int fd() const {return fd_;}
  int flags() const {return curr_flags_;}
  Process * opened_by() {return opened_by_;}
  bool cloexec() const {return curr_flags_ & O_CLOEXEC;}
  void set_cloexec(bool value) {
    if (value) {
      curr_flags_ |= O_CLOEXEC;
    } else {
      curr_flags_ &= ~O_CLOEXEC;
    }
  }
  bool close_on_popen() const {return close_on_popen_;}
  void set_close_on_popen(bool c) {close_on_popen_ = c;}
  bool read() {return read_;}
  bool written() {return written_;}
  const FileName* filename() const {return filename_;}
  void set_pipe(std::shared_ptr<Pipe> pipe) {
    if (pipe_) {
      pipe_->handle_close(this);
    }
    pipe_ = pipe;
  }
  std::shared_ptr<Pipe> pipe() {return pipe_;}
  const std::shared_ptr<Pipe> pipe() const {return pipe_;}

 private:
  int fd_;
  int curr_flags_;
  bool close_on_popen_ = false;
  bool read_ = false;
  bool written_ = false;
  const FileName* filename_;
  std::shared_ptr<Pipe> pipe_;
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

}  /* namespace firebuild */
#endif  // FIREBUILD_FILE_FD_H_
