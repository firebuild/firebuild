#ifndef FIREBUILD_FILEFD_H
#define FIREBUILD_FILEFD_H

#include <fcntl.h>

#include <string>

#include "File.h"
#include "cxx_lang_utils.h"

namespace firebuild 
{
#ifdef __GNUC__
#  include <features.h>
#  if __GNUC_PREREQ(4,8)
  enum fd_origin {FD_ORIGIN_FILE_OPEN, FD_ORIGIN_INTERNAL, FD_ORIGIN_INHERITED};
#  else
  // work around invalid conversion from ‘unsigned char:2’ to ‘firebuild::fd_origin’
  enum {FD_ORIGIN_FILE_OPEN, FD_ORIGIN_INTERNAL, FD_ORIGIN_INHERITED};
  typedef unsigned char fd_origin;
#  endif
#else
  //    If not gcc
  enum fd_origin {FD_ORIGIN_FILE_OPEN, FD_ORIGIN_INTERNAL, FD_ORIGIN_INHERITED};
#endif


  class FileFD
  {
 public:
    int fd;
    int curr_flags;
    fd_origin origin:2;
    bool read : 1;
    bool written : 1;
    /** file descriptor is open (valid) */
    bool open : 1;
    std::string filename;
    FileFD (int fd, int flags, fd_origin o)
        : fd(fd), curr_flags(flags), origin(o)
    {
      if (fd >= 0) {
        open = true;
      }
    };
    /** Constructor for fds obtained through opening files. */
    FileFD (const std::string &f, int fd, int flags)
        :fd(fd), curr_flags(flags), origin(FD_ORIGIN_FILE_OPEN), read(false), written(false),
        open(true), filename(f) {};
    FileFD () : FileFD (-1, 0, FD_ORIGIN_INTERNAL) {};
 private:
  DISALLOW_COPY_AND_ASSIGN(FileFD);
  };
}
#endif
