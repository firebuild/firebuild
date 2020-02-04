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

class ForkedProcess : public Process {
 public:
  explicit ForkedProcess(const int pid, const int ppid, Process* parent);
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
  std::set<std::string>& libs() {
    assert(parent() != NULL);
    return parent()->libs();
  }
  const std::unordered_map<std::string, FileUsage*>& file_usages() const {
    assert(parent() != NULL);
    return parent()->file_usages();
  }
  std::unordered_map<std::string, FileUsage*>& file_usages() {
    return const_cast<std::unordered_map<std::string, FileUsage*>&>
        (static_cast<const ForkedProcess*>(this)->file_usages());
  }
  Process* exec_proc() const {return parent()->exec_proc();};
  int64_t sum_rusage_recurse() {
    set_aggr_time(utime_u() + stime_u());
    return Process::sum_rusage_recurse();
  }

 private:
  virtual void propagate_exit_status(const int) {}
  virtual void disable_shortcutting(const std::string &reason, const Process *p) {
    parent()->disable_shortcutting(reason, p ? p : this);
  }
  virtual bool can_shortcut() const {return false;}
  virtual bool can_shortcut() {return false;}
  DISALLOW_COPY_AND_ASSIGN(ForkedProcess);
};


}  // namespace firebuild
#endif  // FIREBUILD_FORKEDPROCESS_H_
