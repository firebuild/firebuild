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

void ProcessTree::export2dot_recurse(Process &p)
{
  if (p.type == FB_PROC_EXEC_STARTED) {
    export2dot((ExecedProcess&)p);
  }
  if (p.exec_child != NULL) {
    cout << "p" << p.fb_pid << " -> p" << p.exec_child->fb_pid << ";" << endl;
    export2dot_recurse(*p.exec_child);
  }
  for (unsigned int i = 0; i < p.children.size(); i++) {
    export2dot_recurse(*p.children[i]);
  }
}

void ProcessTree::export2dot()
{
  cout << "digraph G {" << endl;
  export2dot_recurse(*root);
  cout << "}" << endl;
}

void ProcessTree::export2dot(ExecedProcess &p)
{
  cout << " subgraph cluster_"<< p.fb_pid << " {" <<endl;
  cout << " label = \"" << p.args[0] << "\";" << endl;
  if (p.children.size() > 0) {
    export2dot((Process&)p, p.children);
  } else {
    cout << " p" << p.fb_pid << ";" << endl;
  }
  cout << "}" << endl;
}

void ProcessTree::export2dot(Process &p, vector<Process*> &children)
{
  for (unsigned int i = 0; i < children.size(); i++) {
    cout << " p" << p.fb_pid << " -> p" << children[i]->fb_pid << ";" << endl;
    export2dot((Process&)*children[i], children[i]->children);
  }
}

}

