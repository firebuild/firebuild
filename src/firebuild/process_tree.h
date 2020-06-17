/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#ifndef FIREBUILD_PROCESS_TREE_H_
#define FIREBUILD_PROCESS_TREE_H_

#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "firebuild/process.h"
#include "firebuild/execed_process.h"
#include "firebuild/forked_process.h"
#include "firebuild/cxx_lang_utils.h"

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
  ProcessTree()
      : sock2proc_(), fb_pid2proc_(), pid2proc_(), pid2fork_parent_fds_(),
        pid2fork_child_sock_(), pid2exec_child_sock_(), cmd_profs_()
  {}
  ~ProcessTree();

  void insert(Process *p, const int sock);
  void insert(ExecedProcess *p, const int sock);
  void finished(const int sock);
  static int64_t sum_rusage_recurse(Process *p);
  void export2js(FILE* stream);
  void export_profile2dot(FILE* stream);
  ExecedProcess* root() {return root_;}
  Process* Sock2Proc(int sock) {
    try {
      return sock2proc_.at(sock);
    } catch (const std::out_of_range& oor) {
      return nullptr;
    }
  }
  Process* pid2proc(int pid) {
    try {
      return pid2proc_.at(pid);
    } catch (const std::out_of_range& oor) {
      return NULL;
    }
  }
  /**
   * Save fork parent's state for the child.
   * @param pid child's PID
   * @param fds file descriptors to be inherited by the child
   */
  void SaveForkParentState(int pid, std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds) {
    pid2fork_parent_fds_[pid] = fds;
  }
  void QueueForkChild(int pid, int sock, int ppid, int ack_num) {
    pid2fork_child_sock_[pid] = {sock, ppid, ack_num};
  }
  void QueueExecChild(int pid, int sock, ExecedProcess* incomplete_child) {
    pid2exec_child_sock_[pid] = {sock, incomplete_child};
  }
  void QueueParentAck(int ppid, int ack, int sock) {
    ppid2pending_parent_ack_[ppid] = {ack, sock};
  }
  std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> Pid2ForkParentFds(const int pid) {
    try {
      return pid2fork_parent_fds_.at(pid);
    } catch (const std::out_of_range& oor) {
      return nullptr;
    }
  }
  const fork_child_sock* Pid2ForkChildSock(const int pid) {
    try {
      return &pid2fork_child_sock_.at(pid);
    } catch (const std::out_of_range& oor) {
      return nullptr;
    }
  }
  const exec_child_sock* Pid2ExecChildSock(const int pid) {
    try {
      return &pid2exec_child_sock_.at(pid);
    } catch (const std::out_of_range& oor) {
      return nullptr;
    }
  }
  const pending_parent_ack* PPid2ParentAck(const int ppid) {
    try {
      return &ppid2pending_parent_ack_.at(ppid);
    } catch (const std::out_of_range& oor) {
      return nullptr;
    }
  }
  void DropForkParentFds(const int pid) {
    pid2fork_parent_fds_.erase(pid);
  }
  void DropQueuedForkChild(const int pid) {
    pid2fork_child_sock_.erase(pid);
  }
  void DropQueuedExecChild(const int pid) {
    pid2fork_child_sock_.erase(pid);
  }
  void DropParentAck(const int ppid) {
    ppid2pending_parent_ack_.erase(ppid);
  }

 private:
  ExecedProcess *root_ = NULL;
  std::unordered_map<int, Process*> sock2proc_;
  std::unordered_map<int, Process*> fb_pid2proc_;
  std::unordered_map<int, Process*> pid2proc_;
  std::unordered_map<int,
                     std::shared_ptr<std::vector<std::shared_ptr<FileFD>>>> pid2fork_parent_fds_;
  std::unordered_map<int, fork_child_sock> pid2fork_child_sock_;
  std::unordered_map<int, exec_child_sock> pid2exec_child_sock_;
  std::unordered_map<int, pending_parent_ack> ppid2pending_parent_ack_ = {};
  /**
   * Profile is aggregated by command name (argv[0]).
   * For each command (C) we store the cumulated CPU time in microseconds
   * (system + user time), and count the invocations of each other command
   * by C. */
  std::unordered_map<std::string, cmd_prof> cmd_profs_;
  void insert_process(Process *p, const int sock);
  void profile_collect_cmds(const Process &p,
                            std::unordered_map<std::string, subcmd_prof> *cmds,
                            std::set<std::string> *ancestors);
  void build_profile(const Process &p, std::set<std::string> *ancestors);

  DISALLOW_COPY_AND_ASSIGN(ProcessTree);
};

}  // namespace firebuild
#endif  // FIREBUILD_PROCESS_TREE_H_
