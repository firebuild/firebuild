/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#ifndef FIREBUILD_FORKED_PROCESS_H_
#define FIREBUILD_FORKED_PROCESS_H_

#include <cassert>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "firebuild/process.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild {

class ExecedProcess;

class ForkedProcess : public Process {
 public:
  explicit ForkedProcess(const int pid, const int ppid, Process* parent,
                         std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds);
  ExecedProcess* exec_point() {return exec_point_;}
  const ExecedProcess* exec_point() const {return exec_point_;}
  /**
   * Fail to change to a working directory
   */
  void handle_fail_wd(const char * const d) {
    assert(parent() != NULL);
    parent()->handle_fail_wd(d);
  }
  /**
   * Record visited working directory
   */
  void add_wd(const FileName *d) {
    assert(parent() != NULL);
    parent()->add_wd(d);
  }
  Process* exec_proc() const {return parent()->exec_proc();}
  int64_t sum_rusage_recurse() {
    set_aggr_time(utime_u() + stime_u());
    return Process::sum_rusage_recurse();
  }

 private:
  ExecedProcess *exec_point_ {};
  virtual void propagate_exit_status(const int) {}
  virtual void disable_shortcutting_only_this(const std::string &reason, const Process *p = NULL) {
    (void) reason;  /* unused */
    (void) p;       /* unused */
  }
  DISALLOW_COPY_AND_ASSIGN(ForkedProcess);
};


}  // namespace firebuild
#endif  // FIREBUILD_FORKED_PROCESS_H_
