/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#ifndef FIREBUILD_FORKEDPROCESS_H_
#define FIREBUILD_FORKEDPROCESS_H_

#include <cassert>
#include <set>
#include <string>

#include "firebuild/process.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild {

class ExecedProcess;

class ForkedProcess : public Process {
 public:
  explicit ForkedProcess(const int pid, const int ppid, Process* parent);
  ExecedProcess* exec_point() {return exec_point_;}
  const ExecedProcess* exec_point() const {return exec_point_;}
  /**
   * Fail to change to a working directory
   */
  void fail_wd(const std::string &d) {
    assert(parent() != NULL);
    parent()->fail_wd(d);
  }
  /**
   * Record visited working directory
   */
  void add_wd(const std::string &d) {
    assert(parent() != NULL);
    parent()->add_wd(d);
  }
  Process* exec_proc() const {return parent()->exec_proc();};
  int64_t sum_rusage_recurse() {
    set_aggr_time(utime_u() + stime_u());
    return Process::sum_rusage_recurse();
  }

 private:
  ExecedProcess *exec_point_ {};
  virtual void propagate_exit_status(const int) {}
  virtual void disable_shortcutting(const std::string &reason, const Process *p = NULL) {
    parent()->disable_shortcutting(reason, p ? p : this);
  }
  DISALLOW_COPY_AND_ASSIGN(ForkedProcess);
};


}  // namespace firebuild
#endif  // FIREBUILD_FORKEDPROCESS_H_
