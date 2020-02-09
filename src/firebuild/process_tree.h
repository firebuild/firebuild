/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#ifndef FIREBUILD_PROCESSTREE_H_
#define FIREBUILD_PROCESSTREE_H_

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <stdexcept>

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

/** Connection of a waiting process that called fork() */
struct fork_parent_sock {
  /** Connection fork parent is waiting on */
  int sock;
  /** ACK number the process is waiting for */
  int ack_num;
};

/** Connection of a waiting fork() child process*/
struct fork_child_sock {
  /** Connection fork child is waiting on */
  int sock;
  /** PID of fork child */
  int pid;
  /** ACK number the process is waiting for */
  int ack_num;
};

class ProcessTree {
 public:
  ProcessTree()
      : sock2proc_(), fb_pid2proc_(), pid2proc_(), pid2fork_parent_sock_(),
        ppid2fork_child_sock_(), cmd_profs_()
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
  void QueueForkParent(int pid, int sock, int ack_num) {
    pid2fork_parent_sock_[pid] = {sock, ack_num};
  }
  void QueueForkChild(int ppid, int sock, int pid, int ack_num) {
    ppid2fork_child_sock_[ppid] = {sock, pid, ack_num};
  }
  const fork_parent_sock* Pid2ForkParentSock(const int pid) {
    try {
      return &pid2fork_parent_sock_.at(pid);
    } catch (const std::out_of_range& oor) {
      return nullptr;
    }
  }
  const fork_child_sock* PPid2ForkChildSock(const int ppid) {
    try {
      return &ppid2fork_child_sock_.at(ppid);
    } catch (const std::out_of_range& oor) {
      return nullptr;
    }
  }
  void DropQueuedForkParent(const int pid) {
    pid2fork_parent_sock_.erase(pid);
  }
  void DropQueuedForkChild(const int ppid) {
    ppid2fork_child_sock_.erase(ppid);
  }

 private:
  ExecedProcess *root_ = NULL;
  std::unordered_map<int, Process*> sock2proc_;
  std::unordered_map<int, Process*> fb_pid2proc_;
  std::unordered_map<int, Process*> pid2proc_;
  std::unordered_map<int, fork_parent_sock> pid2fork_parent_sock_;
  std::unordered_map<int, fork_child_sock> ppid2fork_child_sock_;
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
#endif  // FIREBUILD_PROCESSTREE_H_
