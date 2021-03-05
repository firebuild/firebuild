/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#ifndef FIREBUILD_PROCESS_TREE_H_
#define FIREBUILD_PROCESS_TREE_H_

#include <list>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "firebuild/debug.h"
#include "firebuild/process.h"
#include "firebuild/execed_process.h"
#include "firebuild/forked_process.h"
#include "firebuild/cxx_lang_utils.h"
#include "firebuild/utils.h"

namespace firebuild {

struct subcmd_prof {
  int64_t sum_aggr_time = 0;
  int64_t count = 0;
  bool recursed = false;
};

struct cmd_prof {
  int64_t aggr_time = 0;
  int64_t cmd_time = 0;
  /**  {time_u, count} */
  std::unordered_map<std::string, subcmd_prof> subcmds = {};
};

/** Connection of a waiting fork() child process*/
struct fork_child_sock {
  /** Connection fork child is waiting on */
  int sock;
  /** PID of fork parent */
  int ppid;
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

class ProcessTree {
 public:
  ProcessTree();
  ~ProcessTree();

  std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> inherited_fds() {return inherited_fds_;}
  void insert(Process *p);
  void insert(ExecedProcess *p);
  static int64_t sum_rusage_recurse(Process *p);
  void export2js(FILE* stream);
  void export_profile2dot(FILE* stream);
  ExecedProcess* root() {return root_;}
  Process* pid2proc(int pid) {
    auto it = pid2proc_.find(pid);
    if (it != pid2proc_.end()) {
      return it->second;
    } else {
      return NULL;
    }
  }
  void QueueForkChild(int pid, int sock, int ppid, int ack_num, Process **fork_child_ref) {
    assert(!Pid2ForkChildSock(pid));
    pid2fork_child_sock_[pid] = {sock, ppid, ack_num, fork_child_ref};
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
  const fork_child_sock* Pid2ForkChildSock(const int pid) {
    auto it = pid2fork_child_sock_.find(pid);
    if (it != pid2fork_child_sock_.end()) {
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
  void DropQueuedForkChild(const int pid) {
    pid2fork_child_sock_.erase(pid);
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
  ExecedProcess *root_ = NULL;
  /** This is somewhat analogous to Process::fds_, although cannot change over time.
   *  Represents the fds the root process inherits from the external context.
   *  (The newly execed top process inherits this set here from the ProcessTree,
   *  while a newly execed non-top process inherits its parent's fds_.) */
  std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> inherited_fds_;
  /** The pipes (or terminal lines) inherited from the external world,
   *  each represented by a Pipe object created by this ProcessTree. */
  std::unordered_set<std::shared_ptr<Pipe>> inherited_fd_pipes_;
  std::unordered_map<int, Process*> fb_pid2proc_;
  std::unordered_map<int, Process*> pid2proc_;
  std::unordered_map<int, fork_child_sock> pid2fork_child_sock_;
  /** Whenever an exec*() child appears, but we haven't yet fully processed its exec parent,
   *  we need to put aside the new process until we finish processing its ancestor. */
  std::unordered_map<int, exec_child_sock> pid2exec_child_sock_;
  /** Whenever a posix_spawn*() child process appears, but we haven't yet processed the
   *  posix_spawn_parent message from the parent, we have to put aside the new process until
   *  we get to this point in the parent. The key is the parent's pid. */
  std::unordered_map<int, exec_child_sock> pid2posix_spawn_child_sock_;
  std::unordered_map<int, pending_parent_ack> ppid2pending_parent_ack_ = {};
  /**
   * Profile is aggregated by command name (argv[0]).
   * For each command (C) we store the cumulated CPU time in microseconds
   * (system + user time), and count the invocations of each other command
   * by C. */
  std::unordered_map<std::string, cmd_prof> cmd_profs_;
  void insert_process(Process *p);
  void profile_collect_cmds(const Process &p,
                            std::unordered_map<std::string, subcmd_prof> *cmds,
                            std::set<std::string> *ancestors);
  void build_profile(const Process &p, std::set<std::string> *ancestors);

  DISALLOW_COPY_AND_ASSIGN(ProcessTree);
};

}  // namespace firebuild
#endif  // FIREBUILD_PROCESS_TREE_H_
