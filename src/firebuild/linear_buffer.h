/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

/**
 * Linear buffer implementation optimized for minimizing memory reallocations.
 */

#ifndef FIREBUILD_LINEAR_BUFFER_H_
#define FIREBUILD_LINEAR_BUFFER_H_

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <event2/event.h>

#include <vector>

#include "firebuild/cxx_lang_utils.h"

namespace firebuild {

class LinearBuffer {
 public:
  LinearBuffer()
      : size_(8 * 1024), buffer_(reinterpret_cast<char *>(malloc(size_))), data_start_offset_(0),
        length_(0) {}
  ~LinearBuffer() {free(buffer_);}
  const char * data() const {return &buffer_[data_start_offset_];}
  size_t length() const {return length_;}
  /** Append to the data in the buffer. */
  ssize_t read(evutil_socket_t fd, ssize_t howmuch) {
    if (howmuch < 0) {
      howmuch = readable_bytes(fd);
      if (howmuch <= 0) {
        return howmuch;
      }
    }
    ensure_space(howmuch);
    auto received = ::recv(fd, &buffer_[data_start_offset_ + length_], howmuch, 0);
    if (received > 0) {
      length_ += received;
    }
    return received;
  }
  /** Discard howmuch bytes from the beginning of the data. */
  void discard(const size_t howmuch) {
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
    assert(howmuch >= 0);
    if (data_start_offset_ > 256 * 1024) {
      /* In the unlucky case of not processing all the data for many read cycles move it to the
       * beginning to the buffer to don't inflate the buffer unnecessarily. */
      memmove(buffer_, buffer_ + data_start_offset_, length_);
      data_start_offset_ = 0;
    }
    size_t const needed_size = data_start_offset_ + length_ + howmuch;
    if (size_ < needed_size) {
      size_t new_size = (needed_size > size_ * 2) ? needed_size : size_ * 2;
      buffer_ = reinterpret_cast<char*>(realloc(buffer_, new_size));
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
