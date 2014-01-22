
#include "ForkedProcess.h"

namespace firebuild {
  

ForkedProcess::ForkedProcess (firebuild::msg::ForkChild const & fc,  Process* fork_parent) 
    : Process(fc.pid(), fc.ppid(), FB_PROC_FORK_STARTED,
              (fork_parent)?fork_parent->wd():""),
      fork_parent_(fork_parent)
{
}


}

