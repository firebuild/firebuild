/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/ForkedProcess.h"

#include "firebuild/Debug.h"

namespace firebuild {

ForkedProcess::ForkedProcess(const int pid, const int ppid,
                             Process* fork_parent)
    : Process(pid, ppid, (fork_parent)?fork_parent->wd():"", fork_parent),
      fork_parent_(fork_parent) {
  // add as fork child of parent
  if (fork_parent) {
    fork_parent->children().push_back(this);
  } else {
    fb_error("impossible: Process without known fork parent\n");
  }
}

}  // namespace firebuild

