/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

/**
 * Linear buffer implementation optimized for minimizing memory reallocations.
 */

#ifndef FIREBUILD_LINEAR_BUFFER_H_
#define FIREBUILD_LINEAR_BUFFER_H_

#include <sys/ioctl.h>
#include <sys/types.h>

#include <event2/event.h>

#include <vector>

#include "firebuild/cxx_lang_utils.h"
#include "firebuild/debug.h"

namespace firebuild {

class LinearBuffer {
 public:
  LinearBuffer()
      : size_(8 * 1024), buffer_(reinterpret_cast<char *>(malloc(size_))), data_start_offset_(0),
        length_(0) {}
  ~LinearBuffer() {free(buffer_);}
  const char * data() const {
    return &buffer_[data_start_offset_];
  }
  size_t length() const {return length_;}
  /**
   * Read data from fd and append it to the buffer.
   *
   * Note that in howmuch < 0 case the input buffer may not contain all the data the writer on
   * the other side has written.
   *
   * @param fd file descriptior to read from
   * @param howmuch number of bytes to be read, or in case howmuch is < 0, then read all bytes from
   *        the input buffer
   * @return number of bytes read
   */
  ssize_t read(evutil_socket_t fd, ssize_t howmuch) {
    TRACK(FB_DEBUG_COMM, "fd=%s, howmuch=%ld", D_FD(fd), howmuch);

    assert_cmp(howmuch, !=, 0);
    if (howmuch >= 0) {
      /* Read at most the specified amount, in one step. (Note: fd is nonblocking.) */
      ensure_space(howmuch);
      auto received = ::read(fd, &buffer_[data_start_offset_ + length_], howmuch);
      if (received > 0) {
        length_ += received;
      }
      return received;
    } else {
      /* Read as much as we can. (Note: fd is nonblocking.)
       * Try to use as few system calls as possible on average, see #417.
       * So, begin with a reasonably large read() that will most often result in a short read()
       * and then this is the only syscall we needed to perform. */
      ensure_space(8 * 1024);
      /* Now we have at least 8kB of free space to read to, but maybe even more.
       * Try to read as much as we can, it cannot hurt. */
      const ssize_t attempt1 = size_ - data_start_offset_ - length_;
      auto received1 = ::read(fd, &buffer_[data_start_offset_ + length_], attempt1);
      if (received1 <= 0) {
        /* EOF, or nothing to read right now, or other error. */
        return received1;
      }
      length_ += received1;
      if (received1 < attempt1) {
        /* Short read: return what we already have. */
        return received1;
      }
      /* Full read: need to continue reading.
       * We could expand the buffer and read in a loop until we have everything. But maybe let's
       * just query the incoming data size and read the rest in one step, using two syscalls. */
      const ssize_t attempt2 = readable_bytes(fd);
      if (attempt2 <= 0) {
        /* EOF, or nothing to read right now, or other error. Don't report this, report what we
         * read in the previous step. */
        return received1;
      }
      ensure_space(attempt2);
      auto received2 = ::read(fd, &buffer_[data_start_offset_ + length_], attempt2);
      if (received2 <= 0) {
        /* EOF, or nothing to read right now, or other error. Don't report this, report what we
         * read in the previous step. Or can we assert that this never happens? */
        return received1;
      }
      length_ += received2;
      return received1 + received2;
    }
  }
  /** Discard howmuch bytes from the beginning of the data. */
  void discard(const size_t howmuch) {
    TRACK(FB_DEBUG_COMM, "howmuch=%ld", howmuch);

    assert_cmp(howmuch, <=, length_);
    length_ -= howmuch;
    if (length_ == 0) {
      data_start_offset_ = 0;
    } else {
      data_start_offset_ += howmuch;
    }
  }

 private:
  size_t size_;
  char * buffer_;
  size_t data_start_offset_;
  size_t length_;
  void ensure_space(ssize_t howmuch) {
    TRACK(FB_DEBUG_COMM, "howmuch=%ld", howmuch);

    assert_cmp(howmuch, >=, 0);
    if (data_start_offset_ > 256 * 1024) {
      /* In the unlucky case of not processing all the data for many read cycles move it to the
       * beginning to the buffer to don't inflate the buffer unnecessarily. */
      memmove(buffer_, buffer_ + data_start_offset_, length_);
      data_start_offset_ = 0;
    }
    const size_t needed_size = data_start_offset_ + length_ + howmuch;
    if (size_ < needed_size) {
      size_ = (needed_size > size_ * 2) ? needed_size : size_ * 2;
      buffer_ = reinterpret_cast<char*>(realloc(buffer_, size_));
    }
  }
  ssize_t readable_bytes(evutil_socket_t fd) {
    int n;
    if (ioctl(fd, FIONREAD, &n) < 0) {
      return -1;
    } else {
      return n;
    }
  }
  DISALLOW_COPY_AND_ASSIGN(LinearBuffer);
};

}  // namespace firebuild

#endif  // FIREBUILD_LINEAR_BUFFER_H_
