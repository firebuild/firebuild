#ifndef FIREBUILD_FILEFD_H
#define FIREBUILD_FILEFD_H

#include <fcntl.h>

#include <string>

#include "File.h"
#include "cxx_lang_utils.h"

namespace firebuild {
#ifdef __GNUC__
#  include <features.h>
#  if __GNUC_PREREQ(4, 8)
  enum fd_origin {FD_ORIGIN_FILE_OPEN, FD_ORIGIN_INTERNAL, FD_ORIGIN_INHERITED};
#  else
  // work around invalid conversion from unsigned char:2 to firebuild::fd_origin
  enum {FD_ORIGIN_FILE_OPEN, FD_ORIGIN_INTERNAL, FD_ORIGIN_INHERITED};
  typedef unsigned char fd_origin;
#  endif
#else
  //    If not gcc
  enum fd_origin {FD_ORIGIN_FILE_OPEN, FD_ORIGIN_INTERNAL, FD_ORIGIN_INHERITED};
#endif


class FileFD {
 public:
  FileFD(int fd, int flags, fd_origin o)
      : fd_(fd), curr_flags_(flags), origin_(o), read_(false), written_(false),
      open_((fd_ >= 0)?true:false), filename_() {}
  /** Constructor for fds obtained through opening files. */
  FileFD(const std::string &f, int fd, int flags)
      :fd_(fd), curr_flags_(flags), origin_(FD_ORIGIN_FILE_OPEN), read_(false),
      written_(false), open_(true), filename_(f) {}
  FileFD() : FileFD(-1, 0, FD_ORIGIN_INTERNAL) {}
  int last_err() {return last_err_;}
  void set_last_err(int err) {last_err_ = err;}
  bool open() {return open_;}
  void set_open(bool o) {open_ = o;}

 private:
  int fd_;
  int curr_flags_;
  int last_err_ = 0;
  fd_origin origin_:2;
  bool read_ : 1;
  bool written_ : 1;
  /** file descriptor is open (valid) */
  bool open_ : 1;
  std::string filename_;
  DISALLOW_COPY_AND_ASSIGN(FileFD);
};

}  // namespace firebuild
#endif
