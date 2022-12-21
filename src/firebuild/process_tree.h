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


#ifndef FIREBUILD_PROCESS_TREE_H_
#define FIREBUILD_PROCESS_TREE_H_

#include <tsl/hopscotch_map.h>
#include <tsl/hopscotch_set.h>

#include <filesystem>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "firebuild/debug.h"
#include "firebuild/process.h"
#include "firebuild/execed_process.h"
#include "firebuild/forked_process.h"
#include "firebuild/cxx_lang_utils.h"
#include "firebuild/utils.h"

namespace firebuild {

struct subcmd_prof {
  int64_t sum_aggr_time_u = 0;
  int64_t count = 0;
  bool recursed = false;
};

struct cmd_prof {
  int64_t aggr_time_u = 0;
  int64_t cmd_time_u = 0;
  /**  {time_u, count} */
  tsl::hopscotch_map<std::string, subcmd_prof> subcmds = {};
};

/** Connection of a waiting fork() child process*/
struct fork_child_sock {
  /** Connection fork child is waiting on */
  int sock;
  /** PID of fork child */
  int child_pid;
  /** ACK number the process is waiting for */
  int ack_num;
  /** Location to save child's pointer to after it is created */
  Process** fork_child_ref;
};

/** Connection of a waiting exec() child process*/
struct exec_child_sock {
  /** Connection exec() child is waiting on */
  int sock;
  /** Child data without fds filled */
  ExecedProcess* incomplete_child;
};

/** ACK a parent process is waiting for when the child appears */
struct pending_parent_ack {
  /** ACK number the parent is waiting for */
  int ack_num;
  /** Connection system/popen/posix_spawn parent is waiting on */
  int sock;
};

/** Details about a pending pipe() operation. */
struct pending_pipe_t {
  /** The flags parameter of pipe2() */
  int flags;
  /** The supervisor-side end of fd0, i.e. where the intercepted process reads from
   *  and the supervisor writes to */
  int fd0;
  /*  The supervisor-side end of fd1, i.e. where the intercepted process writes to
   *  and the supervisor reads from */
  int fd1;
};

/** Details about a pending popen() operation. */
struct pending_popen_t {
  /** popen()'s "type", converted to O_* flags. [Set at the opening "popen" message.] */
  int type_flags {0};
  /** The child "sh -c". [Set at the "scproc_query" message.] */
  ExecedProcess *child {nullptr};
  /** Connection fd of the child. [Set at the "scproc_query" message.] */
  int child_conn {-1};
  /** Connection fd of the parent that's performing the popen(). [Set at the "popen_parent"
   *  message.] */
  int parent_conn {-1};
  /** ACK ID to send to the parent. [Set at the "popen_parent" message.] */
  uint16_t ack_num {0};
  /** The client fd of the pipe in the parent. [Set at the "popen_parent" message.] */
  int fd {-1};
};

class ProcessTree {
 public:
  ProcessTree()
      : inherited_fd_pipes_(), fb_pid2proc_(), pid2proc_(), ppid2fork_child_sock_(),
        pid2exec_child_sock_(), pid2posix_spawn_child_sock_(),
        top_dir_(FileName::Get(std::filesystem::current_path())) {
  }
  ~ProcessTree();

  void insert(Process *p);
  void insert_root(pid_t root_pid, int stdin_fd, int stdout_fd, int stderr_fd);
  ForkedProcess* root() {return root_;}
  const FileName* top_dir() const {return top_dir_;}
  Process* pid2proc(int pid) {
    auto it = pid2proc_.find(pid);
    if (it != pid2proc_.end()) {
      return it->second;
    } else {
      return NULL;
    }
  }
  int64_t shortcut_cpu_time_ms() {
    return (root_ && root_->exec_child()) ? root_->exec_child()->shortcut_cpu_time_ms() : 0;
  }
  void QueueForkChild(int pid, int sock, int ppid, int ack_num, Process **fork_child_ref) {
    assert(!Pid2ForkChildSock(ppid));
    ppid2fork_child_sock_[ppid] = {sock, pid, ack_num, fork_child_ref};
  }
  void QueueExecChild(int pid, int sock, ExecedProcess* incomplete_child) {
    pid2exec_child_sock_[pid] = {sock, incomplete_child};
  }
  void QueuePosixSpawnChild(int pid, int sock, ExecedProcess* incomplete_child) {
    pid2posix_spawn_child_sock_[pid] = {sock, incomplete_child};
  }
  void QueueParentAck(int ppid, int ack, int sock) {
    assert(!PPid2ParentAck(ppid));
    ppid2pending_parent_ack_[ppid] = {ack, sock};
  }
  void QueuePendingPipe(Process *proc, pending_pipe_t pending_pipe) {
    assert(!Proc2PendingPipe(proc));
    proc2pending_pipe_[proc] = pending_pipe;
  }
  void QueuePendingPopen(Process *proc, pending_popen_t pending_popen) {
    assert(!Proc2PendingPopen(proc));
    proc2pending_popen_[proc] = pending_popen;
  }
  const fork_child_sock* Pid2ForkChildSock(const int pid) {
    auto it = ppid2fork_child_sock_.find(pid);
    if (it != ppid2fork_child_sock_.end()) {
      return &it->second;
    } else {
      return nullptr;
    }
  }
  const exec_child_sock* Pid2ExecChildSock(const int pid) {
    auto it = pid2exec_child_sock_.find(pid);
    if (it != pid2exec_child_sock_.end()) {
      return &it->second;
    } else {
      return nullptr;
    }
  }
  const exec_child_sock* Pid2PosixSpawnChildSock(const int pid) {
    auto it = pid2posix_spawn_child_sock_.find(pid);
    if (it != pid2posix_spawn_child_sock_.end()) {
      return &it->second;
    } else {
      return nullptr;
    }
  }
  const pending_parent_ack* PPid2ParentAck(const int ppid) {
    auto it = ppid2pending_parent_ack_.find(ppid);
    if (it != ppid2pending_parent_ack_.end()) {
      return &it->second;
    } else {
      return nullptr;
    }
  }
  pending_pipe_t* Proc2PendingPipe(Process *proc) {
    auto it = proc2pending_pipe_.find(proc);
    if (it != proc2pending_pipe_.end()) {
      return &it.value();  /* tsl::hopscotch_map'ism */
    } else {
      return nullptr;
    }
  }
  pending_popen_t* Proc2PendingPopen(Process *proc) {
    auto it = proc2pending_popen_.find(proc);
    if (it != proc2pending_popen_.end()) {
      return &it.value();  /* tsl::hopscotch_map'ism */
    } else {
      return nullptr;
    }
  }
  void DropQueuedForkChild(const int pid) {
    ppid2fork_child_sock_.erase(pid);
  }
  void DropQueuedExecChild(const int pid) {
    pid2exec_child_sock_.erase(pid);
  }
  void DropQueuedPosixSpawnChild(const int pid) {
    pid2posix_spawn_child_sock_.erase(pid);
  }
  void DropParentAck(const int ppid) {
    ppid2pending_parent_ack_.erase(ppid);
  }
  void DropPendingPipe(Process *proc) {
    proc2pending_pipe_.erase(proc);
  }
  void DropPendingPopen(Process *proc) {
    proc2pending_popen_.erase(proc);
  }
  void AckParent(const int ppid) {
    const pending_parent_ack *ack = PPid2ParentAck(ppid);
    if (ack) {
      ack_msg(ack->sock, ack->ack_num);
      DropParentAck(ppid);
    }
  }
  void FinishInheritedFdPipes() {
    for (auto& pipe : inherited_fd_pipes_) {
      pipe->finish();
    }
    /* Destruct these Pipe objects, and in turn their recorders, by dropping the last reference. */
    inherited_fd_pipes_.clear();
  }

 private:
  ForkedProcess *root_ = NULL;
  /** The pipes (or terminal lines) inherited from the external world,
   *  each represented by a Pipe object created by this ProcessTree. */
  tsl::hopscotch_set<std::shared_ptr<Pipe>> inherited_fd_pipes_;
  tsl::hopscotch_map<int, Process*> fb_pid2proc_;
  tsl::hopscotch_map<int, Process*> pid2proc_;
  tsl::hopscotch_map<int, fork_child_sock> ppid2fork_child_sock_;
  /** Whenever an exec*() child appears, but we haven't yet fully processed its exec parent,
   *  we need to put aside the new process until we finish processing its ancestor. */
  tsl::hopscotch_map<int, exec_child_sock> pid2exec_child_sock_;
  /** Whenever a posix_spawn*() child process appears, but we haven't yet processed the
   *  posix_spawn_parent message from the parent, we have to put aside the new process until
   *  we get to this point in the parent. The key is the parent's pid. */
  tsl::hopscotch_map<int, exec_child_sock> pid2posix_spawn_child_sock_;
  tsl::hopscotch_map<int, pending_parent_ack> ppid2pending_parent_ack_ = {};
  /** A process can only have one pending pipe() or pipe2() operation
   *  because the interceptor holds the global mutex for its duration.
   *  Store this rarely used data here to decrease the size of Process objects.
   *  The key is the process that performs the pipe() call. */
  tsl::hopscotch_map<Process *, pending_pipe_t> proc2pending_pipe_ = {};
  /** Although a process can have multiple popen()ed children running in parallel,
   *  it can only have at most one pending popen() operation at a given time.
   *  This is because the parent process holds the global interceptor mutex and waits for an ACK,
   *  and the supervisor only sends that ACK when the child "sh -c" has already appeared.
   *  Store this rarely used data here to decrease the size of Process objects.
   *  The key is the process that performs the popen() call. */
  tsl::hopscotch_map<Process *, pending_popen_t> proc2pending_popen_ = {};
  /**
   * Directory the first executed process starts in. This is presumably the top directory
   * of the project to be built. */
  const FileName* top_dir_ {nullptr};
  void insert_process(Process *p);
  void delete_process_subtree(Process *p);
  void profile_collect_cmds(const Process &p,
                            tsl::hopscotch_map<std::string, subcmd_prof> *cmds,
                            std::set<std::string> *ancestors);

  DISALLOW_COPY_AND_ASSIGN(ProcessTree);
};

/* singleton */
extern ProcessTree *proc_tree;

}  /* namespace firebuild */
#endif  // FIREBUILD_PROCESS_TREE_H_
