/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/ForkedProcess.h"

namespace firebuild {

ForkedProcess::ForkedProcess(const int pid, const int ppid,
                             Process* fork_parent)
    : Process(pid, ppid, (fork_parent)?fork_parent->wd():""),
      fork_parent_(fork_parent)
{
}

}  // namespace firebuild

