
#ifndef FB_PROCESS_TREE_H
#define FB_PROCESS_TREE_H

#include <unordered_map>


#include "Process.h"
#include "ExecedProcess.h"
#include "ForkedProcess.h"

using namespace std;

namespace firebuild 
{

class ProcessTree
{
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
};

}
#endif
