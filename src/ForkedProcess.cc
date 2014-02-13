
#include "ForkedProcess.h"

namespace firebuild {

ForkedProcess::ForkedProcess(const int pid, const int ppid,
                             Process* fork_parent)
    : Process(pid, ppid, FB_PROC_FORK_STARTED,
              (fork_parent)?fork_parent->wd():""),
      fork_parent_(fork_parent)
{
}

}  // namespace firebuild

