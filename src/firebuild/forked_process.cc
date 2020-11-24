/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#include <memory>
#include <vector>

#include "firebuild/file_name.h"
#include "firebuild/forked_process.h"
#include "firebuild/debug.h"

namespace firebuild {

ForkedProcess::ForkedProcess(const int pid, const int ppid,
                             Process* parent,
                             std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds)
    : Process(pid, ppid, parent ? parent->wd() : FileName::Get(""), parent, fds) {
  // add as fork child of parent
  if (parent) {
    exec_point_ = parent->exec_point();
    parent->children().push_back(this);
  } else {
    fb_error("impossible: Process without known fork parent\n");
  }
}

}  // namespace firebuild

