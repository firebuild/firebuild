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
  explicit ForkedProcess(const int pid, const int ppid, Process* fork_parent);
  void set_fork_parent(Process *p) {fork_parent_ = p;}
  Process* fork_parent() {return fork_parent_;}
  /**
   * Fail to change to a working directory
   */
  void fail_wd(const std::string &d) {
    assert(fork_parent_ != NULL);
    fork_parent_->fail_wd(d);
  }
  /**
   * Record visited working directory
   */
  void add_wd(const std::string &d) {
    assert(fork_parent_ != NULL);
    fork_parent_->add_wd(d);
  }
  std::set<std::string>& libs() {
    assert(fork_parent_ != NULL);
    return fork_parent_->libs();
  }
  const std::unordered_map<std::string, FileUsage*>& file_usages() const {
    assert(fork_parent_ != NULL);
    return fork_parent_->file_usages();
  }
  std::unordered_map<std::string, FileUsage*>& file_usages() {
    return const_cast<std::unordered_map<std::string, FileUsage*>&>
        (static_cast<const ForkedProcess*>(this)->file_usages());
  }
  Process* exec_proc() const {return fork_parent_->exec_proc();};
  int64_t sum_rusage_recurse() {
    set_aggr_time(utime_u() + stime_u());
    return Process::sum_rusage_recurse();
  }

 private:
  Process *fork_parent_;
  virtual void propagate_exit_status(const int) {}
  virtual void disable_shortcutting(const std::string &reason) {fork_parent_->propagate_disable_shortcutting(reason, *this);}
  virtual void propagate_disable_shortcutting(const std::string &reason, const Process &p) {fork_parent_->propagate_disable_shortcutting(reason, p);}
  virtual bool can_shortcut() const {return false;}
  virtual bool can_shortcut() {return false;}
  DISALLOW_COPY_AND_ASSIGN(ForkedProcess);
};


}  // namespace firebuild
#endif  // FIREBUILD_FORKEDPROCESS_H_
