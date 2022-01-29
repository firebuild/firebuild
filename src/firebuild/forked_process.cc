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
    : Process(pid, ppid, 0, parent ? parent->wd() : FileName::Get(""), parent, fds, false) {
  TRACKX(FB_DEBUG_PROC, 0, 1, Process, this, "pid=%d, ppid=%d, parent=%s", pid, ppid, D(parent));

  // add as fork child of parent
  if (parent) {
    exec_point_ = parent->exec_point();
    parent->fork_children().push_back(this);
  } else {
    fb_error("impossible: Process without known fork parent\n");
  }
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
        parent()->pid_and_exec_count() + ", " + d(exec_point()->args_to_short_string()) +
        ", fds=" +  d(fds(), level + 1) + "}";
  }
}

}  // namespace firebuild

