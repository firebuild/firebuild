/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/forked_process.h"

#include "firebuild/debug.h"

namespace firebuild {

ForkedProcess::ForkedProcess(const int pid, const int ppid,
                             Process* parent)
    : Process(pid, ppid, (parent)?parent->wd():"", parent) {
  // add as fork child of parent
  if (parent) {
    parent->children().push_back(this);
  } else {
    fb_error("impossible: Process without known fork parent\n");
  }
}

}  // namespace firebuild

