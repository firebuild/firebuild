/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 *
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 * Modification and redistribution are permitted, but commercial use of derivative
 * works is subject to the same requirements of this license
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef FIREBUILD_FILE_FD_H_
#define FIREBUILD_FILE_FD_H_

#include <fcntl.h>

#include <cassert>
#include <memory>
#include <string>

#include "firebuild/file_name.h"
#include "firebuild/cxx_lang_utils.h"
#include "firebuild/pipe.h"

namespace firebuild {

enum fd_type : char {
  FD_UNINITIALIZED,  /* only used intermittently during object construction */
  FD_IGNORED,        /* a path that's on ignore_list, e.g. /dev/null */
  FD_FILE,           /* regular file */
  FD_PIPE_IN,        /* the incoming endpoint of a pipe(), or the toplevel stdin */
  FD_PIPE_OUT,       /* the outgoing endpoint of a pipe(), or the toplevel stdout, stderr */
  FD_SPECIAL,        /* backed by memory, e.g. memfd, eventfd etc. */
  FD_SCM_RIGHTS,     /* received by a recv[m]msg() with SCM_RIGHTS, we don't know its type */
};

class Process;
class Pipe;

/* We don't track these "file creation flags" because fcntl(F_SETFL) ignores them and fcntl(F_GETFL)
 * doesn't report them back. The list is taken from the open(2) manpage.
 * Also O_CLOEXEC is tracked in FileFD where it belongs to, rather than in FileOFD.
 * O_TMPFILE is not listed here, because it is multiple bits and also does not create a named file.
 * */
#define FILE_CREATION_FLAGS (O_CLOEXEC | O_CREAT | O_DIRECTORY | O_EXCL | O_NOCTTY | O_NOFOLLOW | \
                             O_TRUNC)

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
  FileOFD(fd_type type, const FileName *filename, int flags, Process *opened_by);
  ~FileOFD() {
    if (filename_ && is_write(flags_)) {
      filename_->close_for_writing();
    }
  }

  int id() const {return id_;}
  fd_type type() const {return type_;}
  const FileName *filename() const {return filename_;}
  void set_flags(int flags) {
    flags_ = flags & ~(O_ACCMODE | FILE_CREATION_FLAGS);
  }
  int flags() const {return flags_;}
  Process *opened_by() const {return opened_by_;}

 private:
  /* Unique FileOFD id, for debugging. */
  int id_;
  /* Type. */
  fd_type type_;
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
  /** Constructor for fds of a certain type. */
  FileFD(int fd, int flags, fd_type type, Process *opened_by)
      : fd_(fd), ofd_(std::make_shared<FileOFD>(type, nullptr, flags, opened_by)),
        pipe_(), cloexec_(flags & O_CLOEXEC) {
    assert(fd >= 0);
  }
  /** Constructor for fds backed by a pipe including ones created by popen(). */
  FileFD(int fd, int flags, std::shared_ptr<Pipe> pipe, Process *opened_by,
         bool close_on_popen = false)
      : fd_(fd),
        ofd_(std::make_shared<FileOFD>(is_write(flags) ? FD_PIPE_OUT : FD_PIPE_IN,
                                       nullptr, flags, opened_by)),
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
      : fd_(fd),
        ofd_(std::make_shared<FileOFD>(filename->is_in_ignore_location() ? FD_IGNORED : FD_FILE,
                                       filename, flags, opened_by)),
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
  fd_type type() const {return ofd_->type();}
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
  /** If it's a pipe, i.e. type is FD_PIPE_[IN|OUT]. Except for the toplevel stdin where type is
   ** FD_PIPE_IN but pipe_ is NULL. */
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
const char *fd_type_to_string(fd_type type);

}  /* namespace firebuild */
#endif  // FIREBUILD_FILE_FD_H_
