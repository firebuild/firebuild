
#ifndef FIREBUILD_FORKED_PROCESS_H
#define FIREBUILD_FORKED_PROCESS_H

#include "Process.h"

#include "fb-messages.pb.h"
#include "cxx_lang_utils.h"

namespace firebuild 
{
  
class ForkedProcess : public Process
{
 public:
  Process *fork_parent = NULL;
  explicit ForkedProcess (firebuild::msg::ForkChild const & fc);
 private:
  DISALLOW_COPY_AND_ASSIGN(ForkedProcess);
};


}
#endif
