/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#include <memory>
#include <vector>

#include "firebuild/file_name.h"
#include "firebuild/execed_process.h"
#include "firebuild/forked_process.h"
#include "firebuild/debug.h"

namespace firebuild {

ForkedProcess::ForkedProcess(const int pid, const int ppid,
                             Process* parent,
                             std::vector<std::shared_ptr<FileFD>>* fds)
    : Process(pid, ppid, 0, parent ? parent->wd() : FileName::Get(""), parent ? parent->umask() : 0,
              parent, fds) {
  TRACKX(FB_DEBUG_PROC, 0, 1, Process, this, "pid=%d, ppid=%d, parent=%s", pid, ppid, D(parent));

  /* add as fork child of parent */
  if (parent) {
    exec_point_ = parent->exec_point();
    parent->fork_children().push_back(this);
  }
}

void ForkedProcess::set_been_waited_for() {
  assert(!been_waited_for_);
  been_waited_for_ = true;
  /* Try finalizing the process at the bottom of the exec chain. If that succeeds it bubbles up. */
  last_exec_descendant()->maybe_finalize();
}

void ForkedProcess::set_has_orphan_descendant_bubble_up() {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "");
#ifdef FB_EXTRA_DEBUG
  assert(!exec_point() || !exec_point()->can_shortcut());
#endif
  /* This may set has_orphan_descendant_ again, but the bubble up needs to go up to the top
   * for each new orphan to potentially unblock waits. */
  has_orphan_descendant_ = true;
  /* Unblock waits if every descendant is finalized or orphan with terminated ancestors. */
  if (has_on_finalized_ack_set() && can_ack_parent_wait()) {
    maybe_send_on_finalized_ack();
  }
  if (parent()) {
    parent()->fork_point()->set_has_orphan_descendant_bubble_up();
  }
}

void ForkedProcess::maybe_send_on_finalized_ack() {
  if (on_finalized_ack_id_ != -1) {
    assert(on_finalized_ack_fd_ != -1);
    ack_msg(on_finalized_ack_fd_, on_finalized_ack_id_);
    on_finalized_ack_id_ = -1;
    on_finalized_ack_fd_ = -1;
  }
}

void ForkedProcess::do_finalize() {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "");

  /* Now we can ack the previous system()'s second message,
   * or a pending pclose() or wait*(). */
  maybe_send_on_finalized_ack();

  close_fds();

  /* Call the base class's method */
  Process::do_finalize();
}

ForkedProcess::~ForkedProcess() {
  TRACKX(FB_DEBUG_PROC, 1, 0, Process, this, "");
}

/* Member debugging method. Not to be called directly, call the global d(obj_or_ptr) instead.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string ForkedProcess::d_internal(const int level) const {
  if (level > 0) {
    /* brief */
    return Process::d_internal(level);
  } else {
    /* verbose */
    return "{ForkedProcess " + pid_and_exec_count() + ", " + state_string() + ", " +
        (been_waited_for() ? "" : "not ") + "been waited for, parent " +
        (parent() ? parent()->pid_and_exec_count() : "NULL") + ", " +
        (exec_point() ? d(exec_point()->args_to_short_string()) : "<supervisor>") +
        ", fds=" +  d(fds(), level + 1) + "}";
  }
}

}  /* namespace firebuild */

