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

/* We don't track these "file creation flags" because fcntl(F_SETFL) ignores them and fcntl(F_GETFL)
 * doesn't report them back. The list is taken from the open(2) manpage.
 * Also O_CLOEXEC is tracked in FileFD where it belongs to, rather than in FileOFD. */
#define FILE_CREATION_FLAGS (O_CLOEXEC | O_CREAT | O_DIRECTORY | O_EXCL | O_NOCTTY | O_NOFOLLOW | \
                             O_TMPFILE | O_TRUNC)

/**
 * Represents an "open file description" ("ofd") of the intercepted processes, as per the term's
 * definition in POSIX, in the open(2) manual, and in #919. That is, these are the bits that are
 * shared across a dup() or fork().
 *
 * For outgoing pipes, as per #689, Firebuild's intercepting mechanism changes the behavior: across
 * an exec() it undups what are supposed to be dups of each other. Here we model this altered
 * behavior, that is, new OFDs are created upon reopening a pipe.
 *
 * Note: As with Unix pipe()s, the read and the write endpoints are different OFDs.
 */
class FileOFD {
 public:
  FileOFD(const FileName *filename, int flags, Process *opened_by)
      : id_(id_counter_++), filename_(filename), flags_(flags & ~FILE_CREATION_FLAGS),
        opened_by_(opened_by) {
    if (filename_ && is_write(flags_)) {
      filename_->open_for_writing(opened_by_);
    }
  }
  ~FileOFD() {
    if (filename_ && is_write(flags_)) {
      filename_->close_for_writing();
    }
  }

  int id() const {return id_;}
  const FileName *filename() const {return filename_;}
  void set_flags(int flags) {
    flags_ = flags & ~(O_ACCMODE | FILE_CREATION_FLAGS);
  }
  int flags() const {return flags_;}
  Process *opened_by() const {return opened_by_;}

 private:
  /* Unique FileOFD id, for debugging. */
  int id_;
  /** If the file was opened by name during firebuild's supervision. */
  const FileName *filename_;
  /** The open() flags except for O_CLOEXEC, a.k.a. the fcntl(F_GETFL/F_SETFL) flags. */
  int flags_;
  /** Process that opened this file by name.
   *  Remains the same (doesn't get updated to the current process) at dup2() or alike,
   *  also including the case when an outgoing pipe is reopened on an exec().
   *  NULL if the topmost intercepted process already inherited it from the supervisor. */
  Process *opened_by_;

  static int id_counter_;

  DISALLOW_COPY_AND_ASSIGN(FileOFD);
};

/**
 * Represents a "file descriptor" ("fd") of the intercepted process, as per the term's definition in
 * POSIX, in the open(2) manual, and in #919. That is, these are the bits that are _not_ shared
 * across a dup() or fork(), plus a pointer to the shared ("ofd") bits.
 */
class FileFD {
 public:
  /** Constructor for fds backed by internal memory. */
  FileFD(int fd, int flags, Process *opened_by)
      : fd_(fd), ofd_(std::make_shared<FileOFD>(nullptr, flags, opened_by)),
        pipe_(), cloexec_(flags & O_CLOEXEC) {
    assert(fd >= 0);
  }
  /** Constructor for fds backed by a pipe including ones created by popen(). */
  FileFD(int fd, int flags, std::shared_ptr<Pipe> pipe, Process *opened_by,
         bool close_on_popen = false)
      : fd_(fd), ofd_(std::make_shared<FileOFD>(nullptr, flags, opened_by)),
        pipe_(pipe), cloexec_(flags & O_CLOEXEC), close_on_popen_(close_on_popen) {
    assert(fd >= 0);
  }
  /** Constructor for fds created from other fds through dup() or exec() */
  FileFD(int fd, std::shared_ptr<FileFD> ffd_src, bool cloexec)
      : fd_(fd), ofd_(ffd_src->ofd_), pipe_(ffd_src->pipe_), cloexec_(cloexec) {
    assert(fd >= 0);
    if (pipe_) {
      pipe_->handle_dup(ffd_src.get(), this);
    }
  }
  /** Constructor for fds obtained through opening files. */
  FileFD(const FileName* filename, int fd, int flags, Process *opened_by)
      : fd_(fd), ofd_(std::make_shared<FileOFD>(filename, flags, opened_by)),
        pipe_(), cloexec_(flags & O_CLOEXEC) {
    assert(fd >= 0);
  }
  FileFD(const FileFD& other)
      : fd_(other.fd_), ofd_(other.ofd_), pipe_(other.pipe_), cloexec_(other.cloexec_),
        close_on_popen_(other.close_on_popen_) {
  }
  FileFD& operator= (const FileFD& other) {
    fd_ = other.fd_;
    ofd_ = other.ofd_;
    pipe_ = other.pipe_;
    cloexec_ = other.cloexec_;
    close_on_popen_ = other.close_on_popen_;
    return *this;
  }

  /* Getters/setters, some are just convenience proxies to ofd_'s corresponding method. */
  int fd() const {return fd_;}
  std::shared_ptr<FileOFD> ofd() const {return ofd_;}
  const FileName* filename() const {return ofd_->filename();}
  /* Note: This method does NOT change the O_CLOEXEC flag, use set_cloexec() for that. */
  void set_flags(int flags) {ofd_->set_flags(flags);}
  /* Note: This method does NOT report the O_CLOEXEC flag, use cloexec() for that. */
  int flags() const {return ofd_->flags();}
  Process *opened_by() {return ofd_->opened_by();}
  void set_cloexec(bool cloexec) {cloexec_ = cloexec;}
  bool cloexec() const {return cloexec_;}
  bool close_on_popen() const {return close_on_popen_;}
  void set_close_on_popen(bool c) {close_on_popen_ = c;}
  void set_pipe(std::shared_ptr<Pipe> pipe) {
    if (pipe_) {
      pipe_->handle_close(this);
    }
    pipe_ = pipe;
  }
  std::shared_ptr<Pipe> pipe() {return pipe_;}
  const std::shared_ptr<Pipe> pipe() const {return pipe_;}
  /* Like kcmp(KCMP_FILE), checks if the two objects point to the same open file description.
   * Returns -1/0/1 as the usual cmp functions or <=>. */
  int fdcmp(const FileFD& other) const {
    // FIXME Switch to c++20 spaceship: return ofd_ <=> other.ofd_;
    return ofd_ < other.ofd_ ? -1 : ofd_ == other.ofd_ ? 0 : 1;
  }

 private:
  int fd_;
  std::shared_ptr<FileOFD> ofd_;
  /** If it's a pipe. */
  // FIXME Should be moved to FileOFD. Requires nontrivial work around handle_close(), see #939.
  std::shared_ptr<Pipe> pipe_;
  bool cloexec_;
  bool close_on_popen_ {false};
};

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const FileOFD& fofd, const int level = 0);
std::string d(const FileOFD *fofd, const int level = 0);
std::string d(const FileFD& ffd, const int level = 0);
std::string d(const FileFD *ffd, const int level = 0);

}  /* namespace firebuild */
#endif  // FIREBUILD_FILE_FD_H_
