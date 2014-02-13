/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#ifndef FIREBUILD_FORKED_PROCESS_H
#define FIREBUILD_FORKED_PROCESS_H

#include <cassert>
#include <set>
#include <string>

#include "Process.h"
#include "cxx_lang_utils.h"

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
    return const_cast<std::unordered_map<std::string, FileUsage*>&>(static_cast<const ForkedProcess*>(this)->file_usages());
  }

 private:
  Process *fork_parent_;
  DISALLOW_COPY_AND_ASSIGN(ForkedProcess);
};


}  // namespace firebuild
#endif
