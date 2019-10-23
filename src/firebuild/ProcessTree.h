/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#ifndef FIREBUILD_PROCESSTREE_H_
#define FIREBUILD_PROCESSTREE_H_

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <stdexcept>

#include "firebuild/Process.h"
#include "firebuild/ExecedProcess.h"
#include "firebuild/ForkedProcess.h"
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
  /**  {time_m, count} */
  std::unordered_map<std::string, subcmd_prof> subcmds = {};
};

class ProcessTree {
 public:
  ProcessTree()
     : sock2proc_(), fb_pid2proc_(), pid2proc_(), cmd_profs_()
  {}
  ~ProcessTree();

  void insert(Process *p, const int sock);
  void insert(ExecedProcess *p, const int sock);
  void exit(Process *p, const int sock);
  static int64_t sum_rusage_recurse(Process *p);
  void export2js(FILE* stream);
  void export_profile2dot(FILE* stream);
  ExecedProcess* root() {return root_;}
  std::unordered_map<int, Process*>& sock2proc() {return sock2proc_;}
  std::unordered_map<int, Process*>& fb_pid2proc() {return fb_pid2proc_;}
  Process* pid2proc(int pid) {
    try {
      return pid2proc_.at(pid);
    } catch (const std::out_of_range& oor) {
      return NULL;
    }
  }
  Process* find_exec_parent(int pid, int ppid, const std::string &cmd) {
    auto exec_parent = pid2proc(pid);
    if (!exec_parent) {
      exec_parent = pid2proc(ppid);
      if (!exec_parent || !exec_parent->has_running_system_cmd(cmd)) {
        return NULL;
      }
    }
    return exec_parent;
  }

 private:
  ExecedProcess *root_ = NULL;
  std::unordered_map<int, Process*> sock2proc_;
  std::unordered_map<int, Process*> fb_pid2proc_;
  std::unordered_map<int, Process*> pid2proc_;
  /**
   * Profile is aggregated by command name (argv[0]).
   * For each command (C) we store the cumulated CPU time in milliseconds
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
