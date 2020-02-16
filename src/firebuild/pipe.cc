/* Copyright (c) 2020 Interri Kft.
   Author: Balint Reczey <balint@balintreczey.hu>
   This file is an unpublished work. All rights reserved. */

#include "firebuild/pipe.h"

#include <event2/event.h>
#include <fcntl.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <utility>

#include "common/firebuild_common.h"
#include "firebuild/debug.h"

namespace firebuild {

static void pipe_fd0_write_cb(int fd, int16_t what, void *arg);
static void pipe_fd1_read_cb(int fd, int16_t what, void *arg);

Pipe::Pipe(int fd0_conn, int fd1_conn, std::vector<int>&& cache_fds)
    : fd0_event(event_new(ev_base, fd0_conn, EV_WRITE, pipe_fd0_write_cb, this)),
      fd1_ends(), buf_(evbuffer_new()) {
  add_fd1(fd1_conn, std::move(cache_fds));
}

void Pipe::add_fd1(int fd1_conn, std::vector<int>&& cache_fds) {
  assert(fd1_ends.count(fd1_conn) == 0);
  auto fd1_event = event_new(ev_base, fd1_conn, EV_READ, pipe_fd1_read_cb, this);
  fd1_ends[fd1_conn] = new pipe_end({fd1_event, std::move(cache_fds)});
  event_add(fd1_event, NULL);
}

static void pipe_fd0_write_cb(int fd, int16_t what, void *arg) {
  (void) fd; /* unused */
  (void) what; /* FIXME! unused */
  auto pipe = reinterpret_cast<Pipe*>(arg);
  pipe->send_buf();
}

static void close_one_fd1(Pipe *pipe, int fd) {
  auto fd1_end = pipe->fd1_ends[fd];
  close(fd);
  pipe->fd1_ends.erase(fd);
  event_free(fd1_end->ev);
  delete(fd1_end);
  if (pipe->fd1_ends.size() == 0) {
    pipe->finish();
  }
}

static void pipe_fd1_read_cb(int fd, int16_t what, void *arg) {
  (void) what; /* FIXME! unused */
  auto pipe = reinterpret_cast<Pipe*>(arg);
  if (!pipe->buffer_empty()) {
    /* there is still data to send, skip this read */
    return;
  }
  switch (pipe->forward(fd)) {
    case FB_PIPE_WOULDBLOCK: {
      auto this_end = pipe->fd1_ends[fd];
      assert(this_end);
      event_add(this_end->ev, NULL);
      break;
    }
    case FB_PIPE_FD0_EOF: {
      pipe->finish();
      break;
    }
    case FB_PIPE_FD1_EOF: {
      close_one_fd1(pipe, fd);
      break;
    }
    case FB_PIPE_MORE: {
      assert(pipe->fd1_ends.size() > 0);
      /* not scheduling this event again, waiting to be able to send more data on fd0 */
    }
  }
}

void Pipe::only_send() {
  FB_DEBUG(FB_DEBUG_PIPE, "switching pipe to send only mode");
  for (auto it : fd1_ends) {
    if (event_pending(it.second->ev, EV_READ, NULL)) {
      event_del(it.second->ev);
    }
  }
  /* should try again writing when fd0 becomes writable */
  if (!event_pending(fd0_event, EV_WRITE, NULL)) {
    event_add(fd0_event, NULL);
  }
  send_only_mode_ = true;
}

void Pipe::read_again() {
  FB_DEBUG(FB_DEBUG_PIPE, "allowing pipe to read data again");
  for (auto it : fd1_ends) {
    if (!event_pending(it.second->ev, EV_READ, NULL)) {
      event_add(it.second->ev, NULL);
    }
  }
  /* should not try again writing fd0 until data arrives */
  if (event_pending(fd0_event, EV_WRITE, NULL)) {
    event_del(fd0_event);
  }
  send_only_mode_ = false;
}

pipe_state Pipe::send_buf() {
  if (!buffer_empty()) {
    // there is data to be forwarded
    auto fd0_conn = event_get_fd(fd0_event);
    auto sent = evbuffer_write(buf_, fd0_conn);
    FB_DEBUG(FB_DEBUG_PIPE, "sent " + std::to_string(sent) + " bytes via fd: "
             + std::to_string(fd0_conn));
    if (sent == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        /* this pipe should not receive more data */
        only_send();
        return FB_PIPE_WOULDBLOCK;
      } else {
        if (errno != EPIPE) {
          // TODO(rbalint) handle some errors
          perror("write");
        }
        /* unexpected error , the pipe should be cleaned up */
        finish();
        return FB_PIPE_FD0_EOF;
      }
    } else if (sent == 0) {
      /* the single fd0_conn is closed, the pipe should be cleaned up */
      finish();
      return FB_PIPE_FD0_EOF;
    } else {
      if (buffer_empty()) {
        /* buffer emptied, pipe can receive more data */
        if (send_only_mode_) {
          /* this pipe can now receive more data */
          read_again();
        }
        return FB_PIPE_MORE;
      } else {
        // there is remaining data
        only_send();
        return FB_PIPE_WOULDBLOCK;
      }
    }
  } else {
    return FB_PIPE_MORE;
  }
}

pipe_state Pipe::forward(int fd1) {
  if (!fd0_event) {
    return FB_PIPE_FD0_EOF;
  }
  pipe_state send_ret;
  do {
    int received;
    /* Try splice and tee first. */
    if (buffer_empty()) {
      auto fd0_conn = event_get_fd(fd0_event);
      auto fd1_end = fd1_ends[fd1];
      assert(fd1_end);
      auto cache_fds_size = fd1_end->cache_fds.size();
      do {
        if (cache_fds_size > 0) {
          /* Forward data first to block the reader less. */
          received = tee(fd1, fd0_conn, SIZE_MAX, SPLICE_F_NONBLOCK);
          if (received == -1 || received == 0) {
            /* EOF of other error on one of the fds, let the slow path figure that out. */
            break;
          } else {
            FB_DEBUG(FB_DEBUG_PIPE, "sent " + std::to_string(received) + " bytes via fd: "
                     + std::to_string(fd0_conn) + " using tee");
            /* Save the data. */
            for (size_t i = 0; i < cache_fds_size; i++) {
              auto cache_fd = fd1_end->cache_fds[i];
              auto to_save = received;
              do {
                ssize_t saved;
                if (i == cache_fds_size -1) {
                  saved = splice(fd1, NULL, cache_fd, NULL, to_save, 0);
                } else {
                  saved = tee(fd1, cache_fd, to_save, 0);
                }
                assert(saved > 0);
                to_save -= saved;
              } while (to_save > 0);
            }
          }
        } else {
          received = splice(fd1, NULL, fd0_conn, NULL, SIZE_MAX, SPLICE_F_NONBLOCK);
          if (received == -1 || received == 0) {
            /* EOF of other error on one of the fds, let the slow path figure that out. */
            break;
          } else {
            FB_DEBUG(FB_DEBUG_PIPE, "sent " + std::to_string(received) + " bytes via fd: "
                     + std::to_string(fd0_conn) + " using splice");
          }
        }
      } while (received > 0);
    }
    /* Read as much as possible. */
    received = evbuffer_read(buf_, fd1, -1);
    if (received == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return FB_PIPE_WOULDBLOCK;
      } else {
        /* unexpected error, this fd1 connection can be closed */
        return FB_PIPE_FD1_EOF;
      }
    } else if (received == 0) {
      /* pipe end is closed */
      return FB_PIPE_FD1_EOF;
    } else {
      auto fd1_end = fd1_ends[fd1];
      assert(fd1_end);
      if (fd1_end->cache_fds.size() > 0) {
        /* Save the data keeping it in the buffer, too. */
        ssize_t bufsize = evbuffer_get_length(buf_);
        assert(bufsize >= received);
        /* The data to be saved is at the end of the buffer. */
        struct evbuffer_ptr pos;
        evbuffer_ptr_set(buf_, &pos, bufsize - received, EVBUFFER_PTR_SET);
        auto n_vec = evbuffer_peek(buf_, received, &pos, NULL, 0);
        struct evbuffer_iovec vec_out[n_vec];
        for (auto cache_fd : fd1_end->cache_fds) {
#ifndef NDEBUG
          auto copied_out =
#endif
              evbuffer_peek(buf_, received, &pos, vec_out, n_vec);
          assert(copied_out == received);
          fb_writev(cache_fd, vec_out, n_vec);
        }
      }
      send_ret = send_buf();
    }
  } while (send_ret == FB_PIPE_MORE);
  if (send_ret == FB_PIPE_FD0_EOF) {
    assert(!fd0_event);
    return FB_PIPE_FD0_EOF;
  }
  /* sending is blocked, but could read more data */
  assert(fd1_ends.size() > 0);
  return FB_PIPE_MORE;
}

}  // namespace firebuild
