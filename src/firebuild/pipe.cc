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
#include "firebuild/file_fd.h"
#include "firebuild/pipe_recorder.h"
#include "firebuild/process.h"

/** Timeout for closing a pipe after all fd1 ends are closed and a new hasn't been opened. */
const struct timeval kFd1ReopenTimeout = {0, 100000};

namespace firebuild {

static void maybe_finish(Pipe* pipe) {
  TRACKX(FB_DEBUG_PIPE, 1, 1, Pipe, pipe, "");

  if (!pipe->finished()) {
    if (pipe->conn2fd1_ends.size() == 0) {
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
    TRACKX(FB_DEBUG_PIPE, 1, 1, Pipe, pipe, "");

    /* The last FileFD referencing the pipe's fd1 ends is gone, which means all processes that
     * could write to this pipe terminated. */
    pipe->reset_fd0_ptrs_self_ptr_();
    maybe_finish(pipe);
  }
};

struct Fd1Deleter {
  void operator()(Pipe* pipe) const {
    TRACKX(FB_DEBUG_PIPE, 1, 1, Pipe, pipe, "");

    /* The last FileFD referencing the pipe's fd0 ends is gone, which means all processes that
     * could read from this pipe terminated. */
    pipe->reset_fd1_ptrs_self_ptr_();
    maybe_finish(pipe);
  }
};

void Pipe::fd1_timeout_cb(int fd, int16_t what, void *arg) {
  (void) fd; /* unused */
  (void) what;
  assert(what & EV_TIMEOUT);

  Pipe* pipe = reinterpret_cast<Pipe*>(arg);
  if (++pipe->fd1_timeout_round_ >= 2) {
    /* At least kFd1ReopenTimeout time elapsed since the
     * the pipe lost the last fd1 end and all non timer events have been processed after that. */
    pipe->finish();
  } else {
    /* Add it again, it is not persistent. */
    evtimer_add(pipe->fd1_timeout_event_, &kFd1ReopenTimeout);
  }
}

Pipe::Pipe(int fd0_conn, Process* creator)
    : fd0_event(event_new(ev_base, fd0_conn, EV_PERSIST | EV_WRITE, Pipe::pipe_fd0_write_cb, this)),
      conn2fd1_ends(), ffd2fd1_ends(), proc2recorders(), id_(id_counter_++), send_only_mode_(false),
      keep_fd0_open_(false), fd0_shared_ptr_generated_(false), fd1_shared_ptr_generated_(false),
      fd1_timeout_round_(0), buf_(), fd0_ptrs_held_self_ptr_(nullptr),
      fd1_ptrs_held_self_ptr_(nullptr), shared_self_ptr_(this), creator_(creator) {
  TRACKX(FB_DEBUG_PIPE, 0, 1, Pipe, this, "fd0_conn=%s, creator=%s", D_FD(fd0_conn), D(creator));
}

Pipe::~Pipe() {
  TRACKX(FB_DEBUG_PIPE, 1, 0, Pipe, this, "");
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

void Pipe::add_fd1_and_proc(int fd1_conn, FileFD* file_fd, ExecedProcess *proc,
                            std::vector<std::shared_ptr<PipeRecorder>> recorders) {
  TRACKX(FB_DEBUG_PIPE, 1, 1, Pipe, this, "fd1_conn=%s, proc=%s, #recorders=%ld",
         D_FD(fd1_conn), D(proc), recorders.size());

  assert(conn2fd1_ends.count(fd1_conn) == 0);
  assert(!finished());
  if (fd1_timeout_event_) {
    event_free(fd1_timeout_event_);
    fd1_timeout_event_ = nullptr;
  }
  auto fd1_event = event_new(ev_base, fd1_conn, EV_PERSIST | EV_READ, Pipe::pipe_fd1_read_cb, this);
  auto fd1_end = new pipe_end({fd1_event, {file_fd}, recorders, false});
  conn2fd1_ends[fd1_conn] = fd1_end;
  ffd2fd1_ends[file_fd] = fd1_end;
  if (!send_only_mode_) {
    event_add(fd1_event, NULL);
  }
  proc2recorders[proc] = recorders;
}

void Pipe::pipe_fd0_write_cb(int fd, int16_t what, void *arg) {
  auto pipe = reinterpret_cast<Pipe*>(arg);
  TRACKX(FB_DEBUG_PIPE, 1, 1, Pipe, pipe, "fd=%s", D_FD(fd));

  (void) fd; /* unused */
  (void) what; /* FIXME! unused */
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
      if (pipe->buffer_empty() && pipe->conn2fd1_ends.size() == 0
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
  TRACKX(FB_DEBUG_PIPE, 1, 1, Pipe, this, "fd=%s", D_FD(fd));

  auto fd1_end = conn2fd1_ends[fd];
  assert(fd1_end);
  for (auto file_fd : fd1_end->file_fds) {
    ffd2fd1_ends.erase(file_fd);
  }
  conn2fd1_ends.erase(fd);
  event_free(fd1_end->ev);
  close(fd);
  delete(fd1_end);
  if (conn2fd1_ends.size() == 0) {
    if (buffer_empty()) {
      if (!fd1_ptrs_held_self_ptr_) {
        finish();
      } else {
        /* There are references held to fd1 which means that a process may show up inheriting
         * the open pipe end. Set up a timer to close finish() the pipe if the new process does not
         * not register with the supervisor possibly because it is a static binary. */
        assert_null(fd1_timeout_event_);
        fd1_timeout_round_ = 0;
        fd1_timeout_event_ = evtimer_new(ev_base, fd1_timeout_cb, this);
        evtimer_add(fd1_timeout_event_, &kFd1ReopenTimeout);
      }
    } else {
      /* Let the pipe send out the remaining data. */
      set_send_only_mode(true);
    }
  }
}

void Pipe::handle_close(FileFD* file_fd) {
  pipe_end* fd1_end = get_fd1_end(file_fd);
  /* The close message may be processed later than detecting the closure of the pipe end, but
   * when close arrives earlier the end needs to be drained and closed. */
  if (fd1_end) {
    if (fd1_end->file_fds.size() == 1) {
      /* This was the last open fd, it is safe to drain it. */
      drain_fd1_end(file_fd);
    } else {
      ffd2fd1_ends.erase(file_fd);
      fd1_end->file_fds.erase(file_fd);
    }
  }
}

void Pipe::handle_dup(FileFD* old_file_fd, FileFD* new_file_fd) {
  pipe_end* fd1_end = get_fd1_end(old_file_fd);
  /* The dup message may be processed later than detecting the closure of the pipe end,
   * but when a dup arrives and there is an associated end the end should be associated
   * with the new FileFD, too. */
  if (fd1_end) {
    ffd2fd1_ends[new_file_fd] = fd1_end;
    fd1_end->file_fds.insert(new_file_fd);
  }
}

void Pipe::finish() {
  TRACKX(FB_DEBUG_PIPE, 1, 1, Pipe, this, "");

  if (finished()) {
    assert(!shared_self_ptr_);
    return;
  }

  FB_DEBUG(FB_DEBUG_PIPE, "cleaning up " + d(this));
  // clean up all events
  for (auto it : conn2fd1_ends) {
    FB_DEBUG(FB_DEBUG_PIPE, "closing pipe fd1: " + d_fd(it.first));
    event_free(it.second->ev);
    close(it.first);
    delete it.second;
  }
  conn2fd1_ends.clear();
  ffd2fd1_ends.clear();

  pipe_op_result send_ret;
  do {
    send_ret = send_buf();
  } while (!buffer_empty() && send_ret == FB_PIPE_SUCCESS);

  if (!keep_fd0_open_) {
    auto fd0 = event_get_fd(fd0_event);
    FB_DEBUG(FB_DEBUG_PIPE, "closing pipe fd0: " + d_fd(fd0));
    event_free(fd0_event);
    close(fd0);
  } else {
    event_free(fd0_event);
  }
  fd0_event = nullptr;
  if (fd1_timeout_event_) {
    event_free(fd1_timeout_event_);
    fd1_timeout_event_ = nullptr;
  }
  shared_self_ptr_.reset();
}

void Pipe::pipe_fd1_read_cb(int fd, int16_t what, void *arg) {
  auto pipe = reinterpret_cast<Pipe*>(arg);
  TRACKX(FB_DEBUG_PIPE, 1, 1, Pipe, pipe, "fd=%s", D_FD(fd));

  (void) what; /* FIXME! unused */
  auto result = pipe->forward(fd, false, true);
  switch (result) {
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

/**
 * Flip whether we wish to only send data from the Pipe's buffer (which we want if the buffer is
 * nonempty) or if we wish to read (and probably immediately send that). Also configure libevent
 * accordingly.
 *
 * Note: This method can't be called if the current Pipe represents one of regular files the top
 * process inherited for writing. E.g. if you execute:
 *   firebuild command args > outfile
 * then care has to be taken not to call this method on "outfile".
 * This is because libevent's event_add() and friends, and their underlying epoll_ctl() don't
 * support regular files.
 */
void Pipe::set_send_only_mode(const bool mode) {
  TRACKX(FB_DEBUG_PIPE, 1, 0, Pipe, this, "mode=%s", D(mode));

  assert(!finished());
  if (mode) {
    FB_DEBUG(FB_DEBUG_PIPE, "switching " + d(this) + " to send only mode");
    for (auto it : conn2fd1_ends) {
      event_del(it.second->ev);
    }
    /* should try again writing when fd0 becomes writable */
    event_add(fd0_event, NULL);
  } else {
    for (auto it : conn2fd1_ends) {
      event_add(it.second->ev, NULL);
    }
    /* Should not be woken up by fd0 staying writable until data arrives. */
    event_del(fd0_event);
  }
  send_only_mode_ = mode;
}

/**
 * Try to send some of the data that's in the buffers. Also flips send_only_mode (and thus
 * configures libevent) according to whether further sending is needed.
 *
 * The Pipe might represent a regular file that the top process inherited for writing. In this case
 * this method should successfully write the entire buffer, and thus not call set_send_only_mode().
 */
pipe_op_result Pipe::send_buf() {
  TRACKX(FB_DEBUG_PIPE, 1, 1, Pipe, this, "");

  assert(!finished());
  if (!buffer_empty()) {
    /* There is data to be forwarded. */
    auto fd0_conn = event_get_fd(fd0_event);
    ssize_t sent;
    do {
      sent = write(fd0_conn, buf_.data(), buf_.length());
      FB_DEBUG(FB_DEBUG_PIPE, "sent " + d(sent) + " bytes via fd: "
               + d_fd(fd0_conn) + " of " + d(this));
      if (sent == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          /* This pipe should not receive more data. */
          set_send_only_mode(true);
          return FB_PIPE_WOULDBLOCK;
        } else {
          if (errno == EPIPE) {
            FB_DEBUG(FB_DEBUG_PIPE, "ret: FB_PIPE_FD0_EPIPE");
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
        buf_.discard(sent);
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
  TRACKX(FB_DEBUG_PIPE, 1, 1, Pipe, this, "fd1=%s, drain=%s, in_callback=%s",
         D_FD(fd1), D(drain), D(in_callback));

  pipe_op_result send_ret;
  if (finished()) {
    return FB_PIPE_FINISHED;
  }

  auto fd1_end = conn2fd1_ends[fd1];
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
      do {
        /* Forward data first to block the reader less. */
        if (PipeRecorder::has_active_recorder(fd1_end->recorders)) {
          /* We want to record the data. Forward it using tee() which will leave it in the pipe. */
          received = tee(fd1, fd0_conn, SIZE_MAX, SPLICE_F_NONBLOCK);
          if (received == -1 || received == 0) {
            /* EOF of other error on one of the fds, let the slow path figure that out. */
            break;
          } else {
            FB_DEBUG(FB_DEBUG_PIPE, "sent " + d(received) + " bytes from fd: "
                     + d_fd(fd1) + "to fd: " + d_fd(fd0_conn) + " using tee");
            /* Save the data, consuming it from the pipe. */
            PipeRecorder::record_data_from_unix_pipe(&fd1_end->recorders, fd1, received);
          }
        } else {
          /* We do not want to record the data. Forward it using splice() which consumes it from the
           * pipe. */
          received = splice(fd1, NULL, fd0_conn, NULL, SIZE_MAX, SPLICE_F_NONBLOCK);
          if (received == -1 || received == 0) {
            /* EOF of other error on one of the fds, let the slow path figure that out. */
            break;
          } else {
            FB_DEBUG(FB_DEBUG_PIPE, "sent " + d(received) + " bytes to fd: "
                     + d_fd(fd0_conn) + " using splice");
          }
        }

        /* Already successfully read data from the fd, it must have been fully opened. */
        fd1_end->known_to_be_opened = true;
      } while (received > 0);
    }
    /* Read one round to the buffer and try to send it. */
    received = buf_.read(fd1, -1);
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
                     + d_fd(fd1) + " yet");
            ret = FB_PIPE_WOULDBLOCK;
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
      FB_DEBUG(FB_DEBUG_PIPE, "received " + d(received) + " bytes from fd: " + d_fd(fd1));
      /* Locate the new data in the buffer. */
      ssize_t bufsize = buf_.length();
      assert(bufsize >= received);
      const char * buf_to_save = buf_.data() + bufsize - received;
      /* Record it. */
      PipeRecorder::record_data_from_buffer(&fd1_end->recorders, buf_to_save, received);
      /* Try to send it, too. */
      send_ret = send_buf();
    }
  } while (drain && (send_ret != FB_PIPE_FD0_EPIPE));
  if (send_ret == FB_PIPE_FD0_EPIPE || send_ret == FB_PIPE_SUCCESS) {
    return send_ret;
  }
  /* sending is blocked */
  assert(conn2fd1_ends.size() > 0);
  return FB_PIPE_WOULDBLOCK;
}

void Pipe::drain_fd1_end(FileFD* file_fd) {
  TRACKX(FB_DEBUG_PIPE, 1, 1, Pipe, this, "");

  if (finished()) {
    return;
  }
  auto fd1_end = ffd2fd1_ends[file_fd];
  if (!fd1_end) {
    return;
  }
  auto ev = fd1_end->ev;
  assert(ev);
  int fd = event_get_fd(ev);
  switch (forward(fd, true, false)) {
    case FB_PIPE_FD1_EOF: {
      /* This close will not finish the pipe, since there must be an fd1 ptr held, passed to this
         function. */
      close_one_fd1(fd);
      break;
    }
    case FB_PIPE_FD0_EPIPE: {
      if (fd0_event) {
        /* Clean up pipe. */
        finish();
      }
      break;
    }
    default:
      ffd2fd1_ends.erase(file_fd);
      fd1_end->file_fds.erase(file_fd);
  }
}

/* Add the contents of the given file to the Pipe's buffer. This is used when shortcutting a
 * process, the cached data is injected into the Pipe. */
void Pipe::add_data_from_fd(int fd, size_t len) {
  if (len > 0) {
    buf_.read(fd, len);
    /* Pipe might represent one of the top process's files inherited for writing, which might even
     * be a regular file (e.g. in case of "firebuild command args > outfile"). We can't directly
     * call set_send_only_mode() on that. So instead call send_buf(), it'll automatically take care
     * of it. */
    send_buf();
  }
}

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const Pipe& pipe, const int level) {
  std::string ret = "{Pipe #" + d(pipe.id());
  if (level <= 0) {
    if (!pipe.finished()) {
      ret += ", fd1s:";
      for (const auto& it : pipe.conn2fd1_ends) {
        ret += " " + d_fd(it.first);
      }
      ret += ", fd0: " + d_fd(event_get_fd(pipe.fd0_event));
    } else {
      ret += ", finished";
    }
    ret += ", creator=" + d(pipe.creator(), level + 1);
  }
  ret += "}";
  return ret;
}
std::string d(const Pipe *pipe, const int level) {
  if (pipe) {
    return d(*pipe, level);
  } else {
    return "{Pipe NULL}";
  }
}

/* Global counter, so that each Pipe object gets a unique ID. */
int Pipe::id_counter_ = 0;

}  // namespace firebuild
