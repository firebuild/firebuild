/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_PIPE_H_
#define FIREBUILD_PIPE_H_

#include <event2/event.h>
#include <limits.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "firebuild/cxx_lang_utils.h"
#include "firebuild/debug.h"
#include "firebuild/linear_buffer.h"
#include "firebuild/pipe_recorder.h"

extern event_base * ev_base;

namespace firebuild {

class FileFD;
class ExecedProcess;
class Process;

typedef struct _pipe_end {
  /** Event listening on the pipe end */
  struct event* ev;
  /* FileFDs associated with this pipe end keeping a(n fd1) reference to this pipe. */
  std::unordered_set<FileFD*> file_fds;
  /** Cache files to save the captured data to */
  std::vector<std::shared_ptr<PipeRecorder>> recorders;
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
 * The supervisor-side file descriptors of these channels are tracked in conn2fd1_ends, via the
 * add_fd1_and_proc() helper method.
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
 *   the data to fd0 until all the data is sent or received EPIPE on fd0.
 *   Even when the last fd1 gets closed the pipe stays active and a new fd1 can be added to it. This
 *   sequence of events can occur when the supervisor detects the closure of the fd1 fds before a
 *   new intercepted process shows up for which one fd1 end needs to be reopened. As a result pipes
 *   are finished after all fd1 ends are closed and there are no fd1-side references are kept by
 *   processes.
 *   It is also possible that there is an fd1-side reference kept in the supervisor, but the new
 *   process that would inherit it never shows up, for example because it is statically linked
 *   thus it is not intercepted. For that case when all fd1 ends are closed the pipe starts a timer
 *   and waits a preset time and for the processing of all non timer events. If no new fd1 end is
 *   added until this final cutoff time the pipe is finished.
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
  Pipe(int fd0_conn, Process* creator);
  ~Pipe();
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
  /**
   * Fd1 ends indexed by local connection file descriptor.
   * During fd1 end's lifetime this maps the supervisor-side connections to the fd1 end.
   * When and EOF is detected and the fd1 end is cleaned up and the connection is closed
   * the pipe_end reference is also removed from this map. */
  std::unordered_map<int, pipe_end *> conn2fd1_ends;
  /**
   * Fd1 ends indexed by FileFD (pointer)
   * During fd1 end's lifetime this maps the intercepted process' file descriptor as tracked in
   * the supervisor to fd1 ends.
   * When and EOF is detected and the fd1 end is cleaned up the pipe_end reference is also removed
   * from this map. The FileFD can still be tracked as being open, because the message about the
   * close() or dup() may arrive later than the EOF being detected. */
  std::unordered_map<FileFD*, pipe_end *> ffd2fd1_ends;
  /**
   * PipeRecorders indexed by ExecedProcess (pointer)
   *
   * For a given exec point, tells which PipeRecorders record(ed) the subset of the Pipe
   * corresponding to the given ExecProcess.
   * Somewhat similar to conn2fd1_ends and ffd2fd1_ends, but this one has to live on until the
   * process is stored in the cache, when pipe_end might no longer be around. Used for track the
   * recorders across an exec(), as well as storing in the cache what a process wrote to a pipe. */
  std::unordered_map<ExecedProcess *, std::vector<std::shared_ptr<PipeRecorder>>> proc2recorders;

  void add_fd1_and_proc(int fd1, FileFD*, ExecedProcess *proc,
                        std::vector<std::shared_ptr<PipeRecorder>> recorders);
  /**
   * Send contents of the buffer to the 'to' side
   * @return send operation's result
   */
  pipe_op_result send_buf();
  bool buffer_empty() {
    return buf_.length() == 0;
  }
  void reset_fd0_ptrs_self_ptr_() {fd0_ptrs_held_self_ptr_.reset();}
  void reset_fd1_ptrs_self_ptr_() {fd1_ptrs_held_self_ptr_.reset();}
  void set_send_only_mode(bool mode);
  bool send_only_mode() {return send_only_mode_;}
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
  /**
   * Drain one fd1 end corresponding to file_fd and remove file_fd references from ffd2fd1_ends and
   * fd1 end's file_fds if they were present.
   *
   * @return if file_fd references are (possibly earlier) removed from the pipe
   */
  void drain_fd1_end(FileFD* file_fd);
  /**
   * Handle closing a pipe end file descriptor in the intercepted process.
   *
   * Also drain the pipe end if this was the last open fd.
   */
  void handle_close(FileFD* file_fd);
  void handle_dup(FileFD* old_file_fd, FileFD* new_file_fd);
  /** Close all ends of the pipe */
  void finish();
  /** All ends are closed and the pipe is not functional anymore, just exists because there are
   * references to it. */
  bool finished() const {
    return !fd0_event;
  }

  /** Add data from the given fd to the buffer. */
  void add_data_from_fd(int fd, size_t len);

 private:
  /* Unique Pipe ID, for debugging */
  int id_;
  /** Switch send only mode */
  bool send_only_mode_:1;
  bool fd0_shared_ptr_generated_:1;
  bool fd1_shared_ptr_generated_:1;
  /** Number of times the fd1 timeout callback visited the pipe. */
  unsigned int fd1_timeout_round_:3;
  LinearBuffer buf_;
  struct event* fd1_timeout_event_ = nullptr;
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
  /** The process that created this pipe, or NULL if it represents a pipe or terminal line
   *  inherited from the external world. */
  Process* creator_;

  static int id_counter_;
  static void pipe_fd0_write_cb(evutil_socket_t fd, int16_t what, void *arg);
  static void pipe_fd1_read_cb(evutil_socket_t fd, int16_t what, void *arg);
  static void fd1_timeout_cb(int fd, int16_t what, void *arg);
  pipe_end* get_fd1_end(FileFD* file_fd) {
    auto it = ffd2fd1_ends.find(file_fd);
    if (it != ffd2fd1_ends.end()) {
      return it->second;
    } else {
      return nullptr;
    }
  }
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
