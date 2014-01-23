
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
  explicit ForkedProcess (firebuild::msg::ForkChild const & fc, Process* fork_parent);
  void set_fork_parent(Process *p) {fork_parent_ = p;};
  Process* fork_parent() {return fork_parent_;};
  /**
   * Fail to change to a working directory
   */
  void fail_wd(const std::string &d)
  {
    assert(fork_parent_ != NULL);
    fork_parent_->fail_wd(d);
  }
  /**
   * Record visited working directory
   */
  void add_wd(const std::string &d)
  {
    assert(fork_parent_ != NULL);
    fork_parent_->add_wd(d);
  }
  std::set<std::string>& libs()
  {
    assert(fork_parent_ != NULL);
    return fork_parent_->libs();
  };
  std::unordered_map<std::string, FileUsage*>& file_usages() {
    assert(fork_parent_ != NULL);
    return fork_parent_->file_usages();
  };

 private:
  Process *fork_parent_;
  DISALLOW_COPY_AND_ASSIGN(ForkedProcess);
};


}
#endif
