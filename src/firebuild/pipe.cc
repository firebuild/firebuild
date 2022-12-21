/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 *
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 * Modification and redistribution are permitted, but commercial use of derivative
 * works is subject to the same requirements of this license
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "firebuild/pipe.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/epoll.h>
#include <tsl/hopscotch_set.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <string>
#include <utility>

#include "common/firebuild_common.h"
#include "firebuild/debug.h"
#include "firebuild/epoll.h"
#include "firebuild/execed_process.h"
#include "firebuild/file_fd.h"
#include "firebuild/pipe_recorder.h"
#include "firebuild/process.h"
#include "firebuild/process_debug_suppressor.h"

/** Timeout for closing a pipe after all fd1 ends are closed and a new hasn't been opened. */
const int kFd1ReopenTimeoutMs = 100;

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
    maybe_finish(pipe);
    pipe->reset_fd0_ptrs_self_ptr_();
  }
};

struct Fd1Deleter {
  void operator()(Pipe* pipe) const {
    TRACKX(FB_DEBUG_PIPE, 1, 1, Pipe, pipe, "");

    /* The last FileFD referencing the pipe's fd0 ends is gone, which means all processes that
     * could read from this pipe terminated. */
    maybe_finish(pipe);
    pipe->reset_fd1_ptrs_self_ptr_();
  }
};

void Pipe::fd1_timeout_cb(void *arg) {
  Pipe* pipe = reinterpret_cast<Pipe*>(arg);
  ProcessDebugSuppressor debug_suppressor(pipe->creator());
  pipe->fd1_timeout_id_ = -1;
  if (++pipe->fd1_timeout_round_ >= 2) {
    /* At least kFd1ReopenTimeout time elapsed since the
     * the pipe lost the last fd1 end and all non timer events have been processed after that. */
    pipe->finish();
  } else {
    /* Add it again, it is not persistent. */
    pipe->fd1_timeout_id_ = epoll->add_timer(kFd1ReopenTimeoutMs, fd1_timeout_cb, pipe);
  }
}

Pipe::Pipe(int fd0_conn, Process* creator)
    : fd0_conn(fd0_conn),
      conn2fd1_ends(), ffd2fd1_ends(), proc2recorders(), id_(id_counter_++), send_only_mode_(false),
      fd0_shared_ptr_generated_(false), fd1_shared_ptr_generated_(false),
      fd1_timeout_round_(0), buf_(), fd0_ptrs_held_self_ptr_(nullptr),
      fd1_ptrs_held_self_ptr_(nullptr), shared_self_ptr_(this), creator_(creator) {
  TRACKX(FB_DEBUG_PIPE, 0, 1, Pipe, this, "fd0_conn=%s, creator=%s", D_FD(fd0_conn), D(creator));
}

Pipe::~Pipe() {
  TRACKX(FB_DEBUG_PIPE, 1, 0, Pipe, this, "");

  if (fd1_timeout_id_ >= 0) {
    epoll->del_timer(fd1_timeout_id_);
  }
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
  TRACKX(FB_DEBUG_PIPE, 1, 1, Pipe, this, "fd1_conn=%s, proc=%s, #recorders=%" PRIsize,
         D_FD(fd1_conn), D(proc), recorders.size());

#ifdef FB_EXTRA_DEBUG
  assert(conn2fd1_ends.count(fd1_conn) == 0);
#endif
  assert(!finished());
  if (fd1_timeout_id_ >= 0) {
    epoll->del_timer(fd1_timeout_id_);
    fd1_timeout_id_ = -1;
  }

  auto fd1_end = new pipe_end({fd1_conn, {file_fd}, recorders});
  conn2fd1_ends[fd1_conn] = fd1_end;
  ffd2fd1_ends[file_fd] = fd1_end;
  if (!send_only_mode_) {
    epoll->add_fd(fd1_conn, EPOLLIN, Pipe::pipe_fd1_read_cb, this);
  }
  proc2recorders[proc] = recorders;
}

void Pipe::pipe_fd0_write_cb(const struct epoll_event* event, void *arg) {
  auto pipe = reinterpret_cast<Pipe*>(arg);
  ProcessDebugSuppressor debug_suppressor(pipe->creator());
  TRACKX(FB_DEBUG_PIPE, 1, 1, Pipe, pipe, "fd=%s", D_FD(Epoll::event_fd(event)));

  (void) event;  /* unused */
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
      if (pipe->buffer_empty() && pipe->conn2fd1_ends.size() == 0) {
        if (!pipe->fd1_ptrs_held_self_ptr_) {
          /* There are no active fd1 ends nor fd1 references to this pipe. There can't be any more
           * incoming data. */
          pipe->finish();
        } else {
          /* There are references held to fd1 which means that a process may show up inheriting
           * the open pipe end. Set up a timer to finish() the pipe if the new process does
           * not register with the supervisor possibly because it is a static binary. */
          pipe->fd1_timeout_round_ = 0;
          assert(pipe->fd1_timeout_id_ < 0);
          pipe->fd1_timeout_id_ = epoll->add_timer(kFd1ReopenTimeoutMs, fd1_timeout_cb, pipe);
        }
      }
      break;
    }
    default:
      assert(0 && "unexpected result from send_buf()");
  }
}

void Pipe::close_one_fd1(int fd) {
  TRACKX(FB_DEBUG_PIPE, 1, 1, Pipe, this, "fd=%s", D_FD(fd));

  auto it = conn2fd1_ends.find(fd);
  if (it == conn2fd1_ends.end()) {
    return;
  }
  auto fd1_end = it->second;
  for (auto file_fd : fd1_end->file_fds) {
    ffd2fd1_ends.erase(file_fd);
  }
  conn2fd1_ends.erase(it);
  epoll->maybe_del_fd(fd);
  close(fd);
  delete(fd1_end);
  if (conn2fd1_ends.size() == 0) {
    if (buffer_empty()) {
      if (!fd1_ptrs_held_self_ptr_) {
        finish();
      } else {
        /* There are references held to fd1 which means that a process may show up inheriting
         * the open pipe end. Set up a timer to finish() the pipe if the new process does
         * not register with the supervisor possibly because it is a static binary. */
        fd1_timeout_round_ = 0;
        assert(fd1_timeout_id_ < 0);
        fd1_timeout_id_ = epoll->add_timer(kFd1ReopenTimeoutMs, fd1_timeout_cb, this);
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
  /* clean up all events */
  for (auto it : conn2fd1_ends) {
    FB_DEBUG(FB_DEBUG_PIPE, "closing pipe fd1: " + d_fd(it.first));
    epoll->maybe_del_fd(it.first);
    close(it.first);
    delete it.second;
  }
  conn2fd1_ends.clear();
  ffd2fd1_ends.clear();

  pipe_op_result send_ret;
  do {
    send_ret = send_buf();
  } while (!buffer_empty() && send_ret == FB_PIPE_SUCCESS);

  FB_DEBUG(FB_DEBUG_PIPE, "closing pipe fd0: " + d_fd(fd0_conn));
  epoll->maybe_del_fd(fd0_conn);
  close(fd0_conn);
  fd0_conn = -1;

  if (fd1_timeout_id_ >= 0) {
    epoll->del_timer(fd1_timeout_id_);
    fd1_timeout_id_ = -1;
  }
  shared_self_ptr_.reset();
}

void Pipe::pipe_fd1_read_cb(const struct epoll_event* event, void *arg) {
  auto pipe = reinterpret_cast<Pipe*>(arg);
  ProcessDebugSuppressor debug_suppressor(pipe->creator());
  TRACKX(FB_DEBUG_PIPE, 1, 1, Pipe, pipe, "fd=%s", D_FD(Epoll::event_fd(event)));

  auto result = pipe->forward(Epoll::event_fd(event), false);
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
      pipe->close_one_fd1(Epoll::event_fd(event));
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
  TRACKX(FB_DEBUG_PIPE, 1, 0, Pipe, this, "mode=%s", D(mode));

  assert(!finished());
  if (mode != send_only_mode_) {
    FB_DEBUG(FB_DEBUG_PIPE,
             std::string(mode ? "en" : "dis") + "abling send only mode on " + d(this));
    if (mode) {
      for (auto it : conn2fd1_ends) {
        epoll->del_fd(it.first);
      }
      /* should try again writing when fd0 becomes writable */
      epoll->add_fd(fd0_conn, EPOLLOUT, Pipe::pipe_fd0_write_cb, this);
    } else {
      for (auto it : conn2fd1_ends) {
        epoll->add_fd(it.first, EPOLLIN, Pipe::pipe_fd1_read_cb, this);
      }
      /* Should not be woken up by fd0 staying writable until data arrives. */
      epoll->del_fd(fd0_conn);
    }
    send_only_mode_ = mode;
  } else {
    FB_DEBUG(FB_DEBUG_PIPE,
             "send only mode already " + std::string(mode ? "en" : "dis") + "abled on " + d(this));
  }
}

pipe_op_result Pipe::send_buf() {
  TRACKX(FB_DEBUG_PIPE, 1, 1, Pipe, this, "");

  assert(!finished());
  if (!buffer_empty()) {
    /* There is data to be forwarded. */
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
            fb_perror("write");
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

pipe_op_result Pipe::forward(int fd1, bool drain) {
  TRACKX(FB_DEBUG_PIPE, 1, 1, Pipe, this, "fd1=%s, drain=%s",
         D_FD(fd1), D(drain));

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
                     + d_fd(fd1) + " to fd: " + d_fd(fd0_conn) + " using tee");
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
      FB_DEBUG(FB_DEBUG_PIPE, "received EOF from fd: " + d_fd(fd1));
      /* Try emptying the buffer if there is any data to send. */
      send_buf();
      /* pipe end is closed */
      return FB_PIPE_FD1_EOF;
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
  auto fd1_end = get_fd1_end(file_fd);
  if (!fd1_end) {
    return;
  }
  int fd = fd1_end->fd;
  switch (forward(fd, true)) {
    case FB_PIPE_FD1_EOF: {
      /* This close will not finish the pipe, since there must be an fd1 ptr held, passed to this
         function. */
      close_one_fd1(fd);
      break;
    }
    case FB_PIPE_FD0_EPIPE: {
      if (fd0_conn >= 0) {
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

void Pipe::drain() {
  TRACKX(FB_DEBUG_PIPE, 1, 1, Pipe, this, "");

  if (finished()) {
    return;
  }
  bool restart_iteration;
  tsl::hopscotch_set<int> visited_fds;
  do {
    restart_iteration = false;
    for (auto pair : ffd2fd1_ends) {
      pipe_end* fd1_end = pair.second;
      assert(fd1_end);
      int fd = fd1_end->fd;
      if (!visited_fds.insert(fd).second) {
        /* Don't forward traffic again on already visited fds (after restarting iteration). */
        continue;
      }
      switch (forward(fd, true)) {
        case FB_PIPE_FD1_EOF: {
          /* This close will not finish the pipe, since there must be an fd1 ptr held, passed to this
             function. */
          close_one_fd1(fd);
          /* The iterator is invalid now, restart iteration */
          restart_iteration = true;
          break;
        }
        case FB_PIPE_FD0_EPIPE: {
          if (fd0_conn >= 0) {
            /* Clean up pipe. */
            finish();
            /* Break the loop and exit to not touch the invalid iterator again. */
            return;
          }
          break;
        }
        default:
          /* Nothing to do, the fd1 end may keep operating. */
          break;
      }
      if (restart_iteration) {
        break;
      }
    }
  } while (restart_iteration);
}

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
      ret += ", fd0: " + d_fd(pipe.fd0_conn);
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

int Pipe::id_counter_ = 0;

}  /* namespace firebuild */
