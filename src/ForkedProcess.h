
#ifndef FIREBUILD_FORKED_PROCESS_H
#define FIREBUILD_FORKED_PROCESS_H

#include "Process.h"

#include "fb-messages.pb.h"

namespace firebuild 
{
  
class ForkedProcess : public Process
{
 public:
  Process *fork_parent = NULL;
  ForkedProcess (firebuild::msg::ForkChild const & fc);
};


}
#endif
