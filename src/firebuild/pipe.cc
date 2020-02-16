/* Copyright (c) 2020 Interri Kft.
   This file is an unpublished work. All rights reserved. */

#include "firebuild/pipe.h"

#include <event2/event.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <string>
#include <utility>

#include "common/firebuild_common.h"
#include "firebuild/debug.h"

namespace firebuild {

class Process;

static void maybe_finish(Pipe* pipe) {
  if (!pipe->finished()) {
    if (pipe->fd1_ends.size() == 0) {
      if (pipe->buffer_empty()) {
        pipe->finish();
      } else {
        pipe->set_send_only_mode(true);
      }
    }
  }
}

struct Fd0Deleter {
  void operator()(Pipe* pipe) const {
    /* The last FileFD referencing the pipe's fd1 ends is gone, which means all processes that
     * could write to this pipe terminated. */
    pipe->reset_fd0_ptrs_self_ptr_();
    maybe_finish(pipe);
  }
};

struct Fd1Deleter {
  void operator()(Pipe* pipe) const {
    /* The last FileFD referencing the pipe's fd0 ends is gone, which means all processes that
     * could read from this pipe terminated. */
    pipe->reset_fd1_ptrs_self_ptr_();
    maybe_finish(pipe);
  }
};


Pipe::Pipe(int fd0_conn, int fd1_conn, Process* creator, std::vector<int>&& cache_fds)
    : fd0_event(event_new(ev_base, fd0_conn, EV_PERSIST | EV_WRITE, Pipe::pipe_fd0_write_cb, this)),
      fd1_ends(), send_only_mode_(false), keep_fd0_open_(false),
      fd0_shared_ptr_generated_(false), fd1_shared_ptr_generated_(false), buf_(evbuffer_new()),
      fd0_ptrs_held_self_ptr_(nullptr), fd1_ptrs_held_self_ptr_(nullptr),
      shared_self_ptr_(this), creator_(creator) {
  add_fd1(fd1_conn, std::move(cache_fds));
}

std::shared_ptr<Pipe> Pipe::fd0_shared_ptr() {
  assert(!fd0_shared_ptr_generated_);
  fd0_ptrs_held_self_ptr_ = shared_self_ptr_;
  fd0_shared_ptr_generated_ = true;
  return std::shared_ptr<Pipe>(this, Fd0Deleter());
}

std::shared_ptr<Pipe> Pipe::fd1_shared_ptr() {
  assert(!fd1_shared_ptr_generated_);
  fd1_ptrs_held_self_ptr_ = shared_self_ptr_;
  fd1_shared_ptr_generated_ = true;
  return std::shared_ptr<Pipe>(this, Fd1Deleter());
}

void Pipe::add_fd1(int fd1_conn, std::vector<int>&& cache_fds) {
  assert(fd1_ends.count(fd1_conn) == 0);
  auto fd1_event = event_new(ev_base, fd1_conn, EV_PERSIST | EV_READ, Pipe::pipe_fd1_read_cb, this);
  fd1_ends[fd1_conn] = new pipe_end({fd1_event, std::move(cache_fds), false});
  if (!send_only_mode_) {
    event_add(fd1_event, NULL);
  }
}

void Pipe::pipe_fd0_write_cb(int fd, int16_t what, void *arg) {
  (void) fd; /* unused */
  (void) what; /* FIXME! unused */
  auto pipe = reinterpret_cast<Pipe*>(arg);
  switch (pipe->send_buf()) {
    case FB_PIPE_WOULDBLOCK: {
      /* waiting to be able to send more data on fd0 */
      assert(pipe->send_only_mode());
      break;
    }
    case FB_PIPE_FD0_EPIPE: {
      /* Clean up pipe. */
      pipe->finish();
      break;
    }
    case FB_PIPE_SUCCESS: {
      if (pipe->buffer_empty() && pipe->fd1_ends.size() == 0
          && !pipe->fd1_ptrs_held_self_ptr_) {
        /* There are no active fd1 ends nor fd1 references to this pipe. There can't be any more
         * incoming data. */
        pipe->finish();
      }
      break;
    }
    default:
      assert(0 && "unexpected result from send_buf()");
  }
}

void Pipe::close_one_fd1(int fd) {
  auto fd1_end = fd1_ends[fd];
  assert(fd1_end);
  fd1_ends.erase(fd);
  event_free(fd1_end->ev);
  close(fd);
  delete(fd1_end);
  if (fd1_ends.size() == 0) {
    if (buffer_empty()) {
      if (!fd1_ptrs_held_self_ptr_) {
        finish();
      }
    } else {
      /* Let the pipe send out the remaining data. */
      set_send_only_mode(true);
    }
  }
}

void Pipe::finish() {
  if (finished()) {
    assert(!shared_self_ptr_);
    return;
  }

  FB_DEBUG(FB_DEBUG_PIPE, "cleaning up " + to_string());
  // clean up all events
  for (auto it : fd1_ends) {
    FB_DEBUG(FB_DEBUG_PIPE, "closing pipe fd1 fd: " + std::to_string(it.first));
    event_free(it.second->ev);
    close(it.first);
    delete it.second;
  }
  fd1_ends.clear();

  pipe_op_result send_ret;
  do {
    send_ret = send_buf();
  } while (!buffer_empty() && send_ret == FB_PIPE_SUCCESS);

  if (!keep_fd0_open_) {
    auto fd0 = event_get_fd(fd0_event);
    FB_DEBUG(FB_DEBUG_PIPE, "closing pipe fd0 fd: " + std::to_string(fd0));
    event_free(fd0_event);
    close(fd0);
  } else {
    event_free(fd0_event);
  }
  fd0_event = nullptr;
  shared_self_ptr_.reset();
}

void Pipe::pipe_fd1_read_cb(int fd, int16_t what, void *arg) {
  (void) what; /* FIXME! unused */
  auto pipe = reinterpret_cast<Pipe*>(arg);
  switch (pipe->forward(fd, false, true)) {
    case FB_PIPE_WOULDBLOCK: {
      /* waiting to be able to send more data on fd0 */
      assert(pipe->send_only_mode());
      break;
    }
    case FB_PIPE_FD0_EPIPE: {
      pipe->finish();
      break;
    }
    case FB_PIPE_FD1_EOF: {
      pipe->close_one_fd1(fd);
      break;
    }
    case FB_PIPE_SUCCESS: {
      assert(!pipe->send_only_mode());
      break;
    }
    default:
      assert(0 && "unexpected result from forward()");
  }
}

void Pipe::set_send_only_mode(const bool mode) {
  assert(!finished());
  if (mode) {
    FB_DEBUG(FB_DEBUG_PIPE, "switching " + to_string() + " to send only mode");
    for (auto it : fd1_ends) {
      event_del(it.second->ev);
    }
    /* should try again writing when fd0 becomes writable */
    event_add(fd0_event, NULL);
  } else {
    FB_DEBUG(FB_DEBUG_PIPE, "allowing " + to_string() + " to read data again");
    for (auto it : fd1_ends) {
      event_add(it.second->ev, NULL);
    }
    /* Should not be woken up by fd0 staying writable until data arrives. */
    event_del(fd0_event);
  }
  send_only_mode_ = mode;
}

pipe_op_result Pipe::send_buf() {
  assert(!finished());
  if (!buffer_empty()) {
    /* There is data to be forwarded. */
    auto fd0_conn = event_get_fd(fd0_event);
    ssize_t sent;
    do {
      sent = evbuffer_write(buf_, fd0_conn);
      FB_DEBUG(FB_DEBUG_PIPE, "sent " + std::to_string(sent) + " bytes via fd: "
               + std::to_string(fd0_conn) + " of " + to_string());
      if (sent == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          /* This pipe should not receive more data. */
          set_send_only_mode(true);
          return FB_PIPE_WOULDBLOCK;
        } else {
          if (errno == EPIPE) {
            return FB_PIPE_FD0_EPIPE;
          } else {
            // TODO(rbalint) handle some errors
            perror("write");
            return FB_PIPE_FD0_EPIPE;
          }
        }
      } else if (sent == 0) {
        /* This should be handled by EPIPE. */
        assert(0 && "fd0_conn is closed, but not with EPIPE error");
        return FB_PIPE_FD0_EPIPE;
      } else {
        if (buffer_empty()) {
          /* Buffer emptied, pipe can receive more data. */
          if (send_only_mode_) {
            /* This pipe can now receive more data. */
            set_send_only_mode(false);
          }
        }
      }
    } while (sent > 0 && !buffer_empty());
  }
  return FB_PIPE_SUCCESS;
}

pipe_op_result Pipe::forward(int fd1, bool drain, bool in_callback) {
  pipe_op_result send_ret;
  if (finished()) {
    return FB_PIPE_FINISHED;
  }

  auto fd1_end = fd1_ends[fd1];
  assert(fd1_end);

  /* This loop tries to forward as much data as possible without blocking using the fast tee()
   * and splice() calls and then detects which end is blocked by trying to read to the buffer then
   * trying to send it. In case of being called with drain == true fd0 blocking does not break the
   * loop, but it continues to read all data available on fd1. Otherwise fd0 blocking disables read
   * callbacks - which would just fill the buffer - until the buffer is emptied and the data is
   * sent.
   */
  do {
    int received;
    /* Try splice and tee first. */
    if (buffer_empty()) {
      auto fd0_conn = event_get_fd(fd0_event);
      auto cache_fds_size = fd1_end->cache_fds.size();
      do {
        /* Forward data first to block the reader less. */
        if (cache_fds_size > 0) {
          received = tee(fd1, fd0_conn, SIZE_MAX, SPLICE_F_NONBLOCK);
          if (received == -1 || received == 0) {
            /* EOF of other error on one of the fds, let the slow path figure that out. */
            break;
          } else {
            FB_DEBUG(FB_DEBUG_PIPE, "sent " + std::to_string(received) + " bytes from fd: "
                     + std::to_string(fd1) + "to fd: " + std::to_string(fd0_conn) + " using tee");
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
            FB_DEBUG(FB_DEBUG_PIPE, "sent " + std::to_string(received) + " bytes to fd: "
                     + std::to_string(fd0_conn) + " using splice");
          }
        }

        /* Already successfully read data from the fd, it must have been fully opened. */
        fd1_end->known_to_be_opened = true;
      } while (received > 0);
    }
    /* Read one round to the buffer and try to send it. */
    received = evbuffer_read(buf_, fd1, -1);
    if (received == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        /* Try emptying the buffer if there is any data to send. */
        return send_buf();
      } else {
        /* Try emptying the buffer if there is any data to send. */
        send_buf();
        /* unexpected error, this fd1 connection can be closed irrespective to send_buf()'s
         * result */
        return FB_PIPE_FD1_EOF;
      }
    } else if (received == 0) {
        pipe_op_result ret = FB_PIPE_FD1_EOF;
        if (!in_callback && !fd1_end->known_to_be_opened) {
          /* Fd1 never received any data and it received 0 bytes now that can be either mean an EOF
           * or the interceptor may not have opened the other end for writing at all.
           *
           * (Callbacks are called when there was activity on the fd, thus the other side must have
           * opened it, thus there is no need for the extra check for the EOF, 0 in a callback
           * means EOF).
           *
           * Check if there were any activity on the fd using poll(). */
          struct pollfd pfd = {fd1, POLLIN, 0};
          if (poll(&pfd, 1, 0) == 0) {
            /* In case of EOF the pipe has been hung up, therefore poll() would have returned 1 with
               POLLHUP set. Thus the interceptor hasn't opened the other end yet. */
            FB_DEBUG(FB_DEBUG_PIPE, "interceptor has not opened the other end of fd: "
                     + std::to_string(fd1) + " yet");
            ret = FB_PIPE_FD1_ENOTCONN;
            /* There could still be data in the buffer from an other fd1, continue with trying to
             * send it. */
          } else {
            /* There was _some_ activity on the fd, which implies either EOF or new data, because
             * the other end just got connected.
             * By default the situation is already handled as EOF, thus this event does not have
             * to be checked. */
            if (pfd.revents & POLLIN) {
              /* There is data to read. It could occur if the interceptor's end just connected.
                 Try again.*/
              send_ret = FB_PIPE_SUCCESS;
              fd1_end->known_to_be_opened = true;
              continue;
            }
          }
        }
      /* Try emptying the buffer if there is any data to send. */
      send_buf();
      /* pipe end is closed */
      return ret;
    } else {
      FB_DEBUG(FB_DEBUG_PIPE, "received " + std::to_string(received) + " bytes from fd: "
               + std::to_string(fd1));
      if (fd1_end->cache_fds.size() > 0) {
        /* Save the data keeping it in the buffer, too. */
        ssize_t bufsize = evbuffer_get_length(buf_);
        assert(bufsize >= received);
        /* The data to be saved is at the end of the buffer. */
        struct evbuffer_ptr pos;
        evbuffer_ptr_set(buf_, &pos, bufsize - received, EVBUFFER_PTR_SET);
        auto n_vec = evbuffer_peek(buf_, received, &pos, NULL, 0);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-overflow"
        struct evbuffer_iovec vec_out[n_vec];
#pragma GCC diagnostic pop
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
  } while ((drain && (send_ret != FB_PIPE_FD0_EPIPE && send_ret != FB_PIPE_WOULDBLOCK))
           || send_ret == FB_PIPE_SUCCESS);
  if (send_ret == FB_PIPE_FD0_EPIPE) {
    return FB_PIPE_FD0_EPIPE;
  }
  /* sending is blocked */
  assert(fd1_ends.size() > 0);
  return FB_PIPE_WOULDBLOCK;
}

void Pipe::drain_fd1_ends() {
  if (!finished()) {
    /* One round of forwarding should be enough, since the parent can't perform a
     * new write() after the exec() and the forwarding drains the data in flight. */
    // TODO(rbalint) forward only on fds coming from this process.
    for (std::unordered_map<int, pipe_end *>::iterator it = fd1_ends.begin();
         it != fd1_ends.end();) {
      bool finished_with_pipe = false;
      auto fd1_end = it->second;
      auto ev = fd1_end->ev;
      assert(ev);
      int fd = it->first;
      auto res = forward(fd, true, false);
      switch (res) {
        case FB_PIPE_FD1_EOF: {
          /* This part is almost identical to close_one_fd1(), but here close_one_fd1() can't
           * be used because the iteration must continue if the pipe was not cleaned up.
           * Close_one_fd1() would modify fd1_ends and invalidate the iterator. */
          it = fd1_ends.erase(it);
          event_free(ev);
          close(fd);
          delete(fd1_end);
          if (fd1_ends.size() > 0) {
            break;
          } else if (!buffer_empty()) {
            /* There is still buffered data to send out. */
            set_send_only_mode(true);
            finished_with_pipe = true;
            break;
          }
        }
          [[fallthrough]];
        case FB_PIPE_FD0_EPIPE: {
          if (fd0_event) {
            /* Clean up pipe. */
            finish();
          }
          finished_with_pipe = true;
          break;
        }
        case FB_PIPE_FD1_ENOTCONN:
          /* The interceptor hasn't opened the other end yet, move on to next fd1. */
          [[fallthrough]];
        case FB_PIPE_FINISHED:
          [[fallthrough]];
        default:
          it++;
      }
      if (finished_with_pipe) {
        break;
      }
    }
  }
}

std::string Pipe::to_string() const {
  if (!finished()) {
    std::string ret = "pipe with fd1 fds:";
    for (const auto& it : fd1_ends) {
      ret += " " + std::to_string(it.first);
    }
    ret += ", fd0 fd: " + std::to_string(event_get_fd(fd0_event));
    return ret;
  } else {
    return "finished pipe";
  }
}

}  // namespace firebuild
