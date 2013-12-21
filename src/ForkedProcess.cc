
#include "ForkedProcess.h"

namespace firebuild {
  

ForkedProcess::ForkedProcess (firebuild::msg::ForkChild const & fc) 
  : Process(fc.pid(), fc.ppid(), FB_PROC_FORK_STARTED)
{
}


}

