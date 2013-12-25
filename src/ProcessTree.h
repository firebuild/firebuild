
#ifndef FIREBUILD_PROCESS_TREE_H
#define FIREBUILD_PROCESS_TREE_H

#include <set>
#include <unordered_map>
#include <ostream>


#include "Process.h"
#include "ExecedProcess.h"
#include "ForkedProcess.h"
#include "cxx_lang_utils.h"

namespace firebuild 
{

  struct subcmd_prof {
    long int sum_aggr_time;
    long int count;
    bool recursed;
  };

  struct cmd_prof {
    long int aggr_time;
    long int cmd_time;
    std::unordered_map<std::string, subcmd_prof> subcmds; /**<  {time_m, count}*/
  };

  class ProcessTree
  {
 public:
 ProcessTree()
     : sock2proc_(), fb_pid2proc_(), pid2proc_(), cmd_profs_()
    {};
    ~ProcessTree();

    void insert (Process &p, const int sock);
    void insert (ExecedProcess &p, const int sock);
    void insert (ForkedProcess &p, const int sock);
    void exit (Process &p, const int sock);
    static long int sum_rusage_recurse(Process &p);
    void export2js (std::ostream& o);
    void export_profile2dot (std::ostream& o);
    ExecedProcess* root() {return root_;}
    std::unordered_map<int, Process*>& sock2proc() {return sock2proc_;}
    std::unordered_map<int, Process*>& fb_pid2proc() {return fb_pid2proc_;}
    std::unordered_map<int, Process*>& pid2proc() {return pid2proc_;}

 private:
    ExecedProcess *root_ = NULL;
    std::unordered_map<int, Process*> sock2proc_;
    std::unordered_map<int, Process*> fb_pid2proc_;
    std::unordered_map<int, Process*> pid2proc_;
    /**
     * Profile is aggregated by command name (argv[0]).
     * For each command (C) we store the cumulated CPU time in milliseconds
     * (system + user time), and count the invocations of each other command
     * by C. */
    std::unordered_map<std::string, cmd_prof> cmd_profs_;
    void export2js_recurse(Process &p, const unsigned int level, std::ostream& o);
    void export2js(ExecedProcess &p, const unsigned int level, std::ostream& o);
    void profile_collect_cmds(Process &p,
                              std::unordered_map<std::string, subcmd_prof> &cmds,
                              std::set<std::string> &ancestors);
    void build_profile(Process &p, std::set<std::string> &ancestors);

    DISALLOW_COPY_AND_ASSIGN(ProcessTree);
  };

}
#endif
