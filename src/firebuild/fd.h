/* Copyright (c) 2021 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_FD_H_
#define FIREBUILD_FD_H_

#include <cassert>
#include <string>
#include <vector>

#include "firebuild/debug.h"

namespace firebuild {

struct FDAge {
  int seq;
  bool opened;
};

/*
 * An "fd" object represents a server-side file descriptor.
 *
 * It contains the raw file descriptor number, as well as a sequential integer for each fd number.
 * That is, if the same fd is closed and then reopened, it receives a higher sequential number. For
 * example, when fd 7 is first opened, it's represented as "7.1". Once closed and reopened, it is
 * "7.2", and so on. This is useful for two main reasons, as per #433:
 *
 * One is to avoid situations when a delayed message (e.g. ACK to be sent in the future to some fd)
 * would be sent to the wrong channel because the fd has been closed and reopened since.
 *
 * The other is convenient debugging: you can immediately tell whether two events used the same
 * channel, without having to check for close and open events in between.
 *
 * How to use:
 * - open a Unix fd somehow (e.g. accept an incoming connection),
 * - call FD::open(fdnum) to register that it's opened and get an FD object,
 * - carry this FD object instead of the raw fd everywhere where you can,
 * - when needed (i.e. actual file operations), call
 *   - .fd() to get the raw fd, or abort program execution in case of seq mismatch,
 *   - .fd_safe() to get the raw fd, or -1 in case of seq mismatch,
 * - before closing the raw fd, call .close() to register this event,
 * - close the raw fd.
 */
class FD {
 public:
  FD() : fd_(-1), seq_(-1) {}
  FD(const FD& that) : fd_(that.fd()), seq_(that.seq()) {}

  FD& operator=(const FD&) = default;

  /* Register the opening of the FD. Does not actually open anything. */
  static FD open(int fd);
  /* Register the closing of the FD. Does not close the underlying file. */
  void close();
  /* Return the fd if the sequence number is correct, otherwise -1. */
  int fd_safe() const {return is_valid() ? fd_ : -1;}
  /* Return the fd if the sequence number is correct, otherwise abort. */
  int fd() const {assert(is_valid()); return fd_;}
  /* Get the sequence number. */
  int seq() const {return seq_;}

  /* Member debugging method. Not to be called directly, call the global d(obj_or_ptr) instead.
   * level is the nesting level of objects calling each other's d(), bigger means less info to print.
   * See #431 for design and rationale. */
  std::string d_internal(const int level = 0) const;

 private:
  FD(int fd, int seq) : fd_(fd), seq_(seq) {}

  bool is_valid() const {
    return (fd_ >= 0 && (size_t) fd_ < fd_to_age_.size() &&
            fd_to_age_[fd_].opened && fd_to_age_[fd_].seq == seq_);
  }
  static void ensure_fd_in_array(int fd) {
    assert_cmp(fd, >=, 0);
    if (fd_to_age_.size() <= (size_t) fd) {
      fd_to_age_.resize(fd + 1, {.seq = 0, .opened = false});
    }
  }

  int fd_;
  int seq_;

  /* Indexed by the raw fd number, contains the fd's age, that is,
   * the sequential id, plus whether it is registered as open. */
  static std::vector<FDAge> fd_to_age_;
};

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const FD& fd, const int level = 0);
std::string d(const FD *fd, const int level = 0);

}  // namespace firebuild
#endif  // FIREBUILD_FD_H_
