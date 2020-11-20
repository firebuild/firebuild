/* Copyright (c) 2020 Interri Kft.
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
    : fd0_event(event_new(ev_base, fd0_conn, EV_PERSIST | EV_WRITE, pipe_fd0_write_cb, this)),
      fd1_ends(), buf_(evbuffer_new()), shared_self_ptr_(this) {
  add_fd1(fd1_conn, std::move(cache_fds));
}

void Pipe::add_fd1(int fd1_conn, std::vector<int>&& cache_fds) {
  assert(fd1_ends.count(fd1_conn) == 0);
  auto fd1_event = event_new(ev_base, fd1_conn, EV_PERSIST | EV_READ, pipe_fd1_read_cb, this);
  fd1_ends[fd1_conn] = new pipe_end({fd1_event, std::move(cache_fds)});
  event_add(fd1_event, NULL);
}

static void pipe_fd0_write_cb(int fd, int16_t what, void *arg) {
  (void) fd; /* unused */
  (void) what; /* FIXME! unused */
  auto pipe = reinterpret_cast<Pipe*>(arg);
  switch (pipe->send_buf(NULL)) {
    case FB_PIPE_WOULDBLOCK: {
      /* waiting to be able to send more data on fd0 */
      assert(pipe->send_cb_enabled_mode());
      break;
    }
    case FB_PIPE_FD0_EPIPE: {
      /* Clean up pipe. */
      pipe->finish();
      break;
    }
    case FB_PIPE_SUCCESS: {
      assert(!pipe->send_cb_enabled_mode());
      if (pipe->buffer_empty() && pipe->fd1_ends.size() == 0) {
        pipe->finish();
      }
      break;
    }
    default:
      assert(0 && "unexpected result from send_buf()");
  }
}

static void close_one_fd1(Pipe *pipe, int fd) {
  auto fd1_end = pipe->fd1_ends[fd];
  assert(fd1_end);
  close(fd);
  pipe->fd1_ends.erase(fd);
  event_free(fd1_end->ev);
  delete(fd1_end);
  if (pipe->fd1_ends.size() == 0) {
    if (pipe->buffer_empty()) {
      pipe->finish();
    } else {
      /* Let the pipe send out the remaining data. */
      pipe->set_send_cb_enabled_mode(true);
    }
  }
}

void Pipe::finish() {
  FB_DEBUG(FB_DEBUG_PIPE, "cleaning up pipe");
  // clean up all events
  for (auto it : fd1_ends) {
    close(it.first);
    event_free(it.second->ev);
    delete it.second;
  }
  fd1_ends.clear();

  assert(fd0_event);
  close(event_get_fd(fd0_event));
  event_free(fd0_event);
  fd0_event = nullptr;
  shared_self_ptr_.reset();
}

static void pipe_fd1_read_cb(int fd, int16_t what, void *arg) {
  (void) what; /* FIXME! unused */
  auto pipe = reinterpret_cast<Pipe*>(arg);
  switch (pipe->forward(fd, false)) {
    case FB_PIPE_WOULDBLOCK: {
      /* waiting to be able to send more data on fd0 */
      assert(pipe->send_cb_enabled_mode());
      break;
    }
    case FB_PIPE_FD0_EPIPE: {
      pipe->finish();
      break;
    }
    case FB_PIPE_FD1_EOF: {
      close_one_fd1(pipe, fd);
      break;
    }
    case FB_PIPE_SUCCESS: {
      assert(!pipe->send_cb_enabled_mode());
      break;
    }
    default:
      assert(0 && "unexpected result from forward()");
  }
}

void Pipe::set_send_cb_enabled_mode(bool mode) {
  assert(fd0_event);
  if (mode) {
    FB_DEBUG(FB_DEBUG_PIPE, "switching pipe to send callback enabled mode");
    /* should try again writing when fd0 becomes writable */
    event_add(fd0_event, NULL);
  } else {
    FB_DEBUG(FB_DEBUG_PIPE, "allowing pipe to read data again on all fd1s");
    for (auto it : fd1_ends) {
      event_add(it.second->ev, NULL);
    }
    /* should not try again writing fd0 until data arrives */
    event_del(fd0_event);
  }
  send_cb_enabled_mode_ = mode;
}

pipe_op_result Pipe::send_buf(struct event* trigger_fd0_event) {
  assert(fd0_event);
  if (!buffer_empty()) {
    // there is data to be forwarded
    auto fd0_conn = event_get_fd(fd0_event);
    ssize_t sent;
    do {
      sent = evbuffer_write(buf_, fd0_conn);
      FB_DEBUG(FB_DEBUG_PIPE, "sent " + std::to_string(sent) + " bytes via fd: "
               + std::to_string(fd0_conn));
      if (sent == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          /* Should not receive more data from this fd1 to avoid filling the buffer
           * without limit. */
          if (trigger_fd0_event) {
            // xxx event_del(trigger_fd0_event);
          }
          set_send_cb_enabled_mode(true);
          return FB_PIPE_WOULDBLOCK;
        } else {
          if (errno == EPIPE) {
            return FB_PIPE_FD0_EPIPE;
          } else {
            // TODO(rbalint) handle some errors
            perror("write");
          }
        }
      } else if (sent == 0) {
        /* this should be handled by EPIPE */
        assert(0 && "fd0_conn is closed, but not with EPIPE error");
        return FB_PIPE_FD0_EPIPE;
      } else {
        if (buffer_empty()) {
          /* buffer emptied, pipe can receive more data */
          if (send_cb_enabled_mode_) {
            /* this pipe can now receive more data */
            set_send_cb_enabled_mode(false);
          }
        }
      }
    } while (sent > 0 && !buffer_empty());
  }
  return FB_PIPE_SUCCESS;
}

pipe_op_result Pipe::forward(int fd1, bool drain) {
  pipe_op_result send_ret;
  assert(fd0_event);
  /* This loop tries to forward as much data as possible without blocking using the fast tee()
   * and splice() calls and then detects which end is blocked by trying to read to the buffer then
   * trying to send it. In case of not being called from a callback fd0 blocking does not break the
   * loop, but it continues to read all data available on fd1. Otherwise fd0 blocking disables read
   * callbacks - which would just fill the buffer - until the buffer is emptied and the data is
   * sent.
   */
  auto fd1_end = fd1_ends[fd1];
  assert(fd1_end);
  auto fd1_event = fd1_end->ev;
  auto fd0_conn = event_get_fd(fd0_event);
  auto cache_fds_size = fd1_end->cache_fds.size();
  do {
    int received;
    /* Try splice and tee first. */
    if (buffer_empty()) {
      do {
        /* Forward data first to block the reader less. */
        if (cache_fds_size > 0) {
          received = tee(fd1, fd0_conn, SIZE_MAX, SPLICE_F_NONBLOCK);
          if (received == -1 || received == 0) {
            /* EOF of other error on one of the fds, let the slow path figure that out. */
            break;
          } else {
            FB_DEBUG(FB_DEBUG_PIPE, "sent " + std::to_string(received) + " bytes via fd: "
                     + std::to_string(fd0_conn) + " using tee");
            /* Save the data. */
            size_t i;
            for (i = 0; i < cache_fds_size - 1; i++) {
#ifndef NDEBUG
              ssize_t saved =
#endif
                  tee(fd1, fd1_end->cache_fds[i], received, 0);
              assert(saved == received);
            }
#ifndef NDEBUG
            ssize_t saved =
#endif
                splice(fd1, NULL, fd1_end->cache_fds[i], NULL, received, 0);
            assert(saved == received);
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
    /* Read one round to the buffer and try to send it. */
    received = evbuffer_read(buf_, fd1, -1);
    if (received == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        /* Try emptying the buffer if there is any data to send. */
        return send_buf(fd1_event);
      } else {
        /* Try emptying the buffer if there is any data to send. */
        send_buf(fd1_event);
        /* unexpected error, this fd1 connection can be closed irrespective to send_buf()'s
         * result */
        return FB_PIPE_FD1_EOF;
      }
    } else if (received == 0) {
      /* Try emptying the buffer if there is any data to send. */
      send_buf(fd1_event);
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
      send_ret = send_buf(fd1_event);
    }
  } while ((!drain && send_ret != FB_PIPE_FD0_EPIPE) || send_ret == FB_PIPE_SUCCESS);
  if (send_ret == FB_PIPE_FD0_EPIPE) {
    return FB_PIPE_FD0_EPIPE;
  }
  /* sending is blocked */
  assert(fd1_ends.size() > 0);
  return FB_PIPE_WOULDBLOCK;
}

}  // namespace firebuild
