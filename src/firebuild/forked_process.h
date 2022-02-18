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
                         std::vector<std::shared_ptr<FileFD>>* fds);
  virtual ~ForkedProcess();
  ExecedProcess* exec_point() {return exec_point_;}
  ForkedProcess* fork_point() {return this;}
  const ForkedProcess* fork_point() const {return this;}
  const ExecedProcess* exec_point() const {return exec_point_;}
  /**
   * Fail to change to a working directory
   */
  void handle_fail_wd(const char * const d) {
    assert(parent());
    parent()->handle_fail_wd(d);
  }
  /**
   * Record visited working directory
   */
  void add_wd(const FileName *d) {
    assert(parent());
    parent()->add_wd(d);
  }
  Process* exec_proc() const {return parent()->exec_proc();}

  void do_finalize();
  void set_on_finalized_ack(int id, int fd) {
    assert(on_finalized_ack_id_ == -1 && on_finalized_ack_fd_ == -1);
    on_finalized_ack_id_ = id;
    on_finalized_ack_fd_ = fd;
  }
  bool been_waited_for() const {return been_waited_for_;}
  void set_been_waited_for();
  bool orphan() const {return orphan_;}
  void set_orphan() {orphan_ = true;}

  /* Member debugging method. Not to be called directly, call the global d(obj_or_ptr) instead.
   * level is the nesting level of objects calling each other's d(), bigger means less info to print.
   * See #431 for design and rationale. */
  virtual std::string d_internal(const int level = 0) const;

 private:
  ExecedProcess *exec_point_ {};
  void maybe_send_on_finalized_ack();
  int on_finalized_ack_id_ = -1;
  bool been_waited_for_  = false;
  bool orphan_  = false;
  int on_finalized_ack_fd_ = -1;
  virtual void propagate_exit_status(const int) {}
  virtual void disable_shortcutting_only_this(const std::string &reason, const Process *p = NULL) {
    (void) reason;  /* unused */
    (void) p;       /* unused */
  }
  DISALLOW_COPY_AND_ASSIGN(ForkedProcess);
};


}  /* namespace firebuild */
#endif  // FIREBUILD_FORKED_PROCESS_H_
