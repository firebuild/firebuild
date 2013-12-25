
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
  explicit ForkedProcess (firebuild::msg::ForkChild const & fc);
  void set_fork_parent(Process *p) {fork_parent_ = p;};
  Process* fork_parent() {return fork_parent_;};
 private:
  Process *fork_parent_;
  DISALLOW_COPY_AND_ASSIGN(ForkedProcess);
};


}
#endif
