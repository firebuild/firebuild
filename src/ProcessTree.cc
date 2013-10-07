#include <stdexcept>
#include <iostream>

#include "ProcessTree.h"

using namespace std;
namespace firebuild {
  
ProcessTree::~ProcessTree()
{
  // clean up all processes
  for (auto it = fb_pid2proc.begin(); it != fb_pid2proc.end(); ++it) {
    delete(it->second);
  }
}

void ProcessTree::insert (Process &p, const int sock)
{
  sock2proc[sock] = &p;
  fb_pid2proc[p.fb_pid] = &p;
  pid2proc[p.pid] = &p;
}

void ProcessTree::insert (ExecedProcess &p, const int sock)
{
  if (root == NULL) {
    root = &p;
  } else {
    // add as exec child of parent
    try {
      p.exec_parent = pid2proc.at(p.pid);
      p.exec_parent->exec_child = &p;
    } catch (const std::out_of_range& oor) {
      // root's exec_parent is firebuild which is not in the tree.
      // If any other parent is missing, FireBuild missed process
      // that can happen due to the missing process(es) being statically built
      cerr << "TODO handle: Process without known exec parent\n";
    }
  }

  this->insert((Process&)p, sock);
}

void ProcessTree::insert (ForkedProcess &p, const int sock)
{

  // add as fork child of parent
  try {
    p.fork_parent = pid2proc.at(p.ppid);
    p.fork_parent->children.push_back(&p);
  } catch (const std::out_of_range& oor) {
    if (!(*root == p) && (root->pid != p.pid)) {
      // root's fork_parent is firebuild which is not in the tree.
      // If any other parent is missing, FireBuild missed process
      // that can happen due to the missing process(es) being statically built
      cerr << "TODO handle: Process without known parent\n";
    }
  }

  this->insert((Process&)p, sock);
}

void ProcessTree::exit (Process &p, const int sock)
{
  (void)p;
  // TODO maybe this is not needed
  sock2proc.erase(sock);
}

void ProcessTree::export2js_recurse(Process &p, unsigned int level)
{
  if (p.type == FB_PROC_EXEC_STARTED) {
    if (level > 0) {
      cout << endl;
    }
    cout << string(2 * level, ' ') << "{";
    export2js((ExecedProcess&)p, level);
    cout << string(2 * level, ' ') << " children : [";
  }
  if (p.exec_child != NULL) {
    export2js_recurse(*p.exec_child, level + 1);
  }
  for (unsigned int i = 0; i < p.children.size(); i++) {
    export2js_recurse(*p.children[i], level);
  }
  if (p.type == FB_PROC_EXEC_STARTED) {
    if (level == 0) {
      cout << "]};" << endl;
    } else {
      cout << "]},";
    }
  }
}

void ProcessTree::export2js()
{
  cout << "root = ";
  export2js_recurse(*root, 0);
}

void ProcessTree::export2js(ExecedProcess &p, unsigned int level)
{
  unsigned int indent = 2 * level;
  cout << "name :\"" << p.args[0] << "\"," << endl;
  cout << string(indent + 1, ' ') << "id :" << p.fb_pid << "," << endl;
  cout << string(indent + 1, ' ') << "utime_m : " << p.utime_m << "," << endl;
  cout << string(indent + 1, ' ') << "stime_m : " << p.stime_m << "," << endl;
  if (p.state == FB_PROC_FINISHED) {
    cout << string(indent + 1, ' ') << "exit_status : " << p.stime_m << "," << endl;
  }
}

}

