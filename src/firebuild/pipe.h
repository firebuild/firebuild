/* Copyright (c) 2020 Interri Kft. */
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
  FB_PIPE_SUCCESS
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
 * - Pipe::forward(int fd1, bool drain) can be used to reading from an fd1 end with or without
 *   draining it. It tries to read once, or all the readable data in case of draining it.
 *   Pipe::forward() reads from fd1 irrespective to the send_only_mode_ state, possibly adding more
 *   data to the already used buffer. Drain mode is used when trying to receive all sent
 *   data from a process that exec()-ed or terminated.
 *
 * Pipe ends lifecycle:
 * - Fd1 ends can be closed independently. When one fd1 end is closed the file descriptor is closed,
 *   the callback on it is disabled and freed. When the last fd1 is closed there may still be data
 *   in the buffer to send. In that case the pipe switches to send_only_mode_ and keeps forwarding
 *   the data to fd0 until all the data is sent or receives EPIPE on fd0. After either of those the
 *   whole Pipe can be finish()-ed, cleaning up all ends.
 * - When fd0 end is closed the whole Pipe can be finish()-ed discarding the buffered data. This is
 *   detected when receiving EPIPE on fd0.
 * The forward() and send_buf() functions don't change the Pipe ends, it is the responsibility of
 * the caller of forward() and send_buf() based on the Pipe operation result.
 */
class Pipe {
 public:
  Pipe(int fd0_conn, int fd1_conn, std::vector<int>&& cache_fds);
  ~Pipe() {evbuffer_free(buf_);}

  /**
   * Shared_ptr of this Pipe.
   *
   * Only use this shared_ptr and don't make new ones, otherwise the pipe gets freed too early!
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
  void set_send_only_mode(bool mode);
  bool send_only_mode() {return send_only_mode_;}
  /**
   * Read from fd1 and try to forward it to fd0
   * @param fd1 connection to read from
   * @param drain true: read only while there is data to read false: read till EOF
   * @return result of the read or write operation, whichever could be executed last
   */
  pipe_op_result forward(int fd1, bool drain);
  /** Close all ends of the pipe */
  void finish();

 private:
  /** Switch send only mode */
  bool send_only_mode_ = false;
  struct evbuffer * buf_;
  /** Shared self pointer to clean oneself up only after finish(). */
  std::shared_ptr<Pipe> shared_self_ptr_;
  DISALLOW_COPY_AND_ASSIGN(Pipe);
};

}  // namespace firebuild
#endif  // FIREBUILD_PIPE_H_
