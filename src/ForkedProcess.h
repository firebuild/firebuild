
#ifndef FB_FORKED_PROCESS_H
#define FB_FORKED_PROCESS_H

#include "Process.h"

#include "fb-messages.pb.h"

using namespace std;

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
