/* Copyright (c) 2019 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_PIPE_H_
#define FIREBUILD_PIPE_H_

#include <event2/buffer.h>
#include <event2/event.h>
#include <limits.h>
#include <unistd.h>

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "firebuild/debug.h"
#include "firebuild/file_fd.h"

extern event_base * ev_base;

namespace firebuild {

typedef struct _pipe_end {
  /** Event listening on the pipe end */
  struct event* ev;
  /** Cache files to save the captured data to */
  std::vector<int> cache_fds;
} pipe_end;

typedef enum {
  /** Pipe fd would block forwarding more data */
  FB_PIPE_WOULDBLOCK,
  /** Pipe fd reached EOF or got EPIPE on the fd0 tried */
  FB_PIPE_FD0_EOF,
  /** Pipe fd reached EOF or got EPIPE on the fd1 tried */
  FB_PIPE_FD1_EOF,
  /** The fd can accept more data */
  FB_PIPE_MORE
} pipe_state;

class Pipe {
 public:
  Pipe(int fd0_conn, int fd1_conn, std::vector<int>&& cache_fds);
  ~Pipe() {evbuffer_free(buf_);}

  struct event * fd0_event;
  std::unordered_map<int, pipe_end *> fd1_ends;

  void add_fd1(int fd1, std::vector<int>&& cache_fds);
  /** Send contents of the buffer to the 'to' side */
  pipe_state send_buf();
  bool buffer_empty() {
    return evbuffer_get_length(buf_) == 0;
  }
  pipe_state forward(int fd1);
  /** Close all ends of the pipe */
  void finish() {
    FB_DEBUG(FB_DEBUG_PIPE, "cleaning up pipe");
    // clean up all events
    for (auto it : fd1_ends) {
      close(it.first);
      event_free(it.second->ev);
      delete it.second;
    }
    fd1_ends.clear();
    if (fd0_event) {
      close(event_get_fd(fd0_event));
      event_free(fd0_event);
      fd0_event = nullptr;
    }
  }

 private:
  bool send_only_mode_ = false;
  void only_send();
  void read_again();
  struct evbuffer * buf_;
  DISALLOW_COPY_AND_ASSIGN(Pipe);
};

}  // namespace firebuild
#endif  // FIREBUILD_PIPE_H_
