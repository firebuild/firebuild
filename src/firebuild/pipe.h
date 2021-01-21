/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_PIPE_H_
#define FIREBUILD_PIPE_H_

#include <event2/buffer.h>
#include <event2/event.h>
#include <limits.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "firebuild/cxx_lang_utils.h"
#include "firebuild/debug.h"

extern event_base * ev_base;

namespace firebuild {

class Process;

typedef struct _pipe_end {
  /** Event listening on the pipe end */
  struct event* ev;
  /** Cache files to save the captured data to */
  std::vector<int> cache_fds;
  bool known_to_be_opened;
} pipe_end;

/**
 * Result codes of operations performed on pipe ends.
 */
typedef enum {
  /** Pipe's fd0 end would block forwarding more data */
  FB_PIPE_WOULDBLOCK,
  /** Pipe's fd0 end got EPIPE */
  FB_PIPE_FD0_EPIPE,
  /** One of pipe's fd1 reached EOF */
  FB_PIPE_FD1_EOF,
  /** The pipe end can accept more data */
  FB_PIPE_SUCCESS,
  /** Pipe is already finished, it is not operational. */
  FB_PIPE_FINISHED
} pipe_op_result;

/**
 * A single Pipe object represents what would be a single Unix unnamed pipe (fifo) without the
 * interceptor mimicking it for the intercepted program. The interceptor routes the data written
 * to the pipe through the supervisor to be able to record it. Pipe is also used to catch the
 * initial stdout and stderr of the topmost intercepted process (typically the terminal).
 *
 * A Pipe may have multiple source file descriptors (fd1-s), that could be written to by multiple
 * Processes, due to dup(), fork() and alike. Each of them are converted to a separate named pipe
 * towards the supervisor, because it needs to record which process wrote the data.
 * The supervisor-side file descriptors of these channels are tracked in fd1_ends, via the
 * add_fd1() helper method.
 *
 * The fd0 and fd1 naming in the supervisor reflects that in the intercepted programs those ends
 * are connected to the pipefd[0] and pipefd[1] of pipe()'s output parameter. See pipe(2).
 *
 * Each Pipe has a single fd0 end in the supervisor. While filefd[0] can also be read from via
 * multiple file descriptors, even by multiple intercepted processes, the supevisor does not track
 * those separately because those are inputs to the intercepted processes and it cannot be reliably
 * separated on the supervisor's side which process consumed which part of the data. (As a result
 * expected process inputs read from pipes or inherited file descriptors cannot be used when
 * shortcutting a single process.)
 *
 * Forwarding data on the supervisor's side can be event-triggered or forced by calling
 * Pipe::forward():
 * - For the event-triggered method there is a libevent callback registered on each pipe end.
 *   fd0 and fd1 ends have different event handlers due fd0 can only be written to, and fd1-s can
 *   only be read. In Pipe's default state (send_only_mode_ == false) the fd1 ends' callback is
 *   active and whenever there is incoming data on an fd1 end it is written to the fd0 end
 *   (and saved if the process the data came from can be shortcut). The data is not buffered if
 *   it can be immediately sent. In this mode fd0's callback is disabled.
 *
 *   If the incoming data can't be immediately sent via fd0 because fd0 would block
 *   the pipe enters send_only_mode_, enables the callback on fd0 to be notified when fd0 becomes
 *   writable again, and disables callbacks on fd1-s to not receive more data to the internal
 *   buffer (buf_), where the data in flight is saved.
 *
 *   In send_only_mode_ only writes to fd0 are triggered by fd events and the Pipe stays in this
 *   mode until the internal buffer is emptied. Then the fd0 callback is disabled and all fd1
 *   callbacks are enabled again. send_only_mode_ is set to false.
 *
 * - Pipe::forward(int fd1, bool drain, bool in_callback) can be used to reading from an fd1 end
 *   with or without draining it. It tries to read once, or all the readable data in case of
 *   draining it.
 *   Pipe::forward() reads from fd1 irrespective to the send_only_mode_ state, possibly adding more
 *   data to the already used buffer. Drain mode is used when trying to receive all sent
 *   data from a process that exec()-ed or terminated.
 *
 * Pipe ends lifecycle:
 * - Fd1 ends can be closed independently. When one fd1 end is closed the file descriptor is closed,
 *   the callback on it is disabled and freed. When the last fd1 is closed there may still be data
 *   in the buffer to send. In that case the pipe switches to send_only_mode_ and keeps forwarding
 *   the data to fd0 until all the data is sent or received EPIPE on fd0. Even when the last fd1
 *   gets closed the pipe stays active and a new fd1 can be added to it. This sequence of events can
 *   occur when the supervisor detects the closure of the fd1 fds before a new intercepted process
 *   shows up for which one fd1 end needs to be reopened. As a result pipes are finished only after
 *   all fd1 ends are closed and there are no fd1-side references are kept by processes.
 * - When fd0 end is closed the whole Pipe can be finish()-ed discarding the buffered data and
 *   closing all fd1 ends. This is detected when receiving EPIPE on fd0.
 * The forward() and send_buf() functions don't change the Pipe ends, it is the responsibility of
 * the caller of forward() and send_buf() based on the Pipe operation result.
 *
 * Pipe end connection lifecycle:
 *  1. The remote side hasn't opened it yet. read() returns 0, poll() gives no POLLHUP.
 *  2. It's open by the remote side. read() returns != 0, poll() gives no POLLHUP.
 *  3. It's closed by the remote side, but there's still data buffered.
 *     read() returns != 0, poll() gives POLLHUP.
 *  4. It's closed by the remote side, the buffer is emptied (i.e. EOF). read() returns 0,
 *     poll() gives POLLHUP.
 *  If we've encountered state 2 or 3 or 4 then we flip pipe_end.known_to_be_opened for a faster
 *  code path. Otherwise poll()'s POLLHUP allows to distinguish case 1 from case 4.
 */
class Pipe {
 public:
  Pipe(int fd0_conn, int fd1_conn, Process* creator, std::vector<int>&& cache_fds);
  ~Pipe() {evbuffer_free(buf_);}

  /**
   * Shared_ptr of this Pipe for fd0-side references.
   */
  std::shared_ptr<Pipe> fd0_shared_ptr();
  /**
   * Shared_ptr of this Pipe for fd1-side references.
   */
  std::shared_ptr<Pipe> fd1_shared_ptr();
  /**
   * Shared_ptr of this Pipe for not fd0- or fd1-side references.
   */
  std::shared_ptr<Pipe> shared_ptr() {return shared_self_ptr_;}
  /**
   * Event with the callback triggered when fd0 end is writable.
   *
   * Cleaned up and set to nullptr in finish() only.
   */
  struct event * fd0_event;
  /** Fd1 ends indexed by local connection file descriptor */
  std::unordered_map<int, pipe_end *> fd1_ends;

  void add_fd1(int fd1, std::vector<int>&& cache_fds);
  /**
   * Send contents of the buffer to the 'to' side
   * @return send operation's result
   */
  pipe_op_result send_buf();
  bool buffer_empty() {
    return evbuffer_get_length(buf_) == 0;
  }
  void reset_fd0_ptrs_self_ptr_() {fd0_ptrs_held_self_ptr_.reset();}
  void reset_fd1_ptrs_self_ptr_() {fd1_ptrs_held_self_ptr_.reset();}
  void set_send_only_mode(bool mode);
  bool send_only_mode() {return send_only_mode_;}
  void set_keep_fd0_open() {keep_fd0_open_ = true;}
  int id() const {return id_;}
  const Process * creator() const {return creator_;}
  /**
   * Read from fd1 and try to forward it to fd0
   * @param fd1 connection to read from
   * @param drain false: read() available data only once true: read till EOF
   * @param in_callback: called from an event callback
   * @return result of the read or write operation, whichever could be executed last
   */
  pipe_op_result forward(int fd1, bool drain, bool in_callback);
  void drain_fd1_ends();
  /** Close all ends of the pipe */
  void finish();
  /** All ends are closed and the pipe is not functional anymore, just exists because there are
   * references to it. */
  bool finished() const {
    return !fd0_event;
  }

 private:
  /* Unique Pipe ID, for debugging */
  int id_;
  /** Switch send only mode */
  bool send_only_mode_:1;
  bool keep_fd0_open_:1;
  bool fd0_shared_ptr_generated_:1;
  bool fd1_shared_ptr_generated_:1;
  struct evbuffer * buf_;
  /**
   * Shared self pointer used by fd0 references to clean oneself up only after finish() and keep
   * track of fd0 references separately . */
  std::shared_ptr<Pipe> fd0_ptrs_held_self_ptr_;
  /**
   * Shared self pointer used by fd1 references to clean oneself up only after finish() and keep
   * track of fd1 references separately. */
  std::shared_ptr<Pipe> fd1_ptrs_held_self_ptr_;
  /** Shared self pointer kept until the pipe is finish()-ed */
  std::shared_ptr<Pipe> shared_self_ptr_;
  /** The process that created this pipe */
  Process* creator_;

  static int id_counter_;
  static void pipe_fd0_write_cb(evutil_socket_t fd, int16_t what, void *arg);
  static void pipe_fd1_read_cb(evutil_socket_t fd, int16_t what, void *arg);
  void close_one_fd1(int fd);
  DISALLOW_COPY_AND_ASSIGN(Pipe);
};

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const Pipe& pipe, const int level = 0);
std::string d(const Pipe *pipe, const int level = 0);

}  // namespace firebuild
#endif  // FIREBUILD_PIPE_H_
