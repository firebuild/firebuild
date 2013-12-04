
#ifndef FIREBUILD_PROCESS_TREE_H
#define FIREBUILD_PROCESS_TREE_H

#include <set>
#include <unordered_map>
#include <ostream>


#include "Process.h"
#include "ExecedProcess.h"
#include "ForkedProcess.h"

using namespace std;

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
    unordered_map<string, subcmd_prof> subcmds; /**<  {time_m, count}*/
  };

  class ProcessTree
  {
 private:
    /**
     * Profile is aggregated by command name (argv[0]).
     * For each command (C) we store the cumulated CPU time in milliseconds
     * (system + user time), and count the invocations of each other command
     * by C. */
    unordered_map<string, cmd_prof> cmd_profs;
    void export2js_recurse(Process &p, unsigned int level, ostream& o);
    void export2js(ExecedProcess &p, unsigned int level, ostream& o);
    void profile_collect_cmds(Process &p,
                              unordered_map<string, subcmd_prof> &cmds,
                              set<string> &ancestors);
    void build_profile(Process &p, set<string> &ancestors);

 public:
    ExecedProcess *root = NULL;
    unordered_map<int, Process*> sock2proc;
    unordered_map<int, Process*> fb_pid2proc;
    unordered_map<int, Process*> pid2proc;
    ~ProcessTree();

    void insert (Process &p, const int sock);
    void insert (ExecedProcess &p, const int sock);
    void insert (ForkedProcess &p, const int sock);
    void exit (Process &p, const int sock);
    long int sum_rusage_recurse(Process &p);
    void export2js (ostream& o);
    void export_profile2dot (ostream& o);
  };

}
#endif
