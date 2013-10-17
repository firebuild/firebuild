#include <stdexcept>
#include <iostream>
#include <sstream>

#include "ProcessTree.h"

using namespace std;
namespace firebuild {
  
/**
 * Escape string for JavaScript
 * from http://stackoverflow.com/questions/7724448/simple-json-string-escape-for-c
 * TODO: use JSONCpp instead to handle all cases
 */
static std::string escapeJsonString(const std::string& input) {
  std::ostringstream ss;
  for (auto iter = input.cbegin(); iter != input.cend(); iter++) {
    switch (*iter) {
    case '\\': ss << "\\\\"; break;
    case '"': ss << "\\\""; break;
    case '/': ss << "\\/"; break;
    case '\b': ss << "\\b"; break;
    case '\f': ss << "\\f"; break;
    case '\n': ss << "\\n"; break;
    case '\r': ss << "\\r"; break;
    case '\t': ss << "\\t"; break;
    default: ss << *iter; break;
    }
  }
  return ss.str();
}

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
      p.exec_parent->state = FB_PROC_EXECED;
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

void ProcessTree::sum_rusage_recurse(Process &p)
{
  if (p.type == FB_PROC_EXEC_STARTED) {
    ExecedProcess *e = (ExecedProcess*)&p;
    e->sum_rusage(&e->sum_utime_m,
                 &e->sum_stime_m);
    if (e->exec_parent) {
      e->sum_utime_m -= e->exec_parent->utime_m;
      e->sum_stime_m -= e->exec_parent->stime_m;
    }
  }
  if (p.exec_child != NULL) {
    sum_rusage_recurse(*p.exec_child);
  }
  for (unsigned int i = 0; i < p.children.size(); i++) {
    sum_rusage_recurse(*p.children[i]);
  }
}

void ProcessTree::export2js_recurse(Process &p, unsigned int level, ostream& o)
{
  if (p.type == FB_PROC_EXEC_STARTED) {
    if (level > 0) {
      o << endl;
    }
    o << string(2 * level, ' ') << "{";
    export2js((ExecedProcess&)p, level, o);
    o << string(2 * level, ' ') << " children : [";
  }
  if (p.exec_child != NULL) {
    export2js_recurse(*p.exec_child, level + 1, o);
  }
  for (unsigned int i = 0; i < p.children.size(); i++) {
    export2js_recurse(*p.children[i], level, o);
  }
  if (p.type == FB_PROC_EXEC_STARTED) {
    if (level == 0) {
      o << "]};" << endl;
    } else {
      o << "]},";
    }
  }
}

void ProcessTree::export2js(ostream& o)
{
  o << "root = ";
  export2js_recurse(*root, 0, o);
}

void ProcessTree::export2js(ExecedProcess &p, unsigned int level, ostream& o)
{
  // TODO: escape all strings properly
  unsigned int indent = 2 * level;
  o << "name :\"" << p.args[0] << "\"," << endl;
  o << string(indent + 1, ' ') << "id :" << p.fb_pid << "," << endl;
  o << string(indent + 1, ' ') << "pid :" << p.pid << "," << endl;
  o << string(indent + 1, ' ') << "ppid :" << p.ppid << "," << endl;
  o << string(indent + 1, ' ') << "cwd :\"" << p.cwd << "\"," << endl;
  o << string(indent + 1, ' ') << "exe :\"" << p.executable << "\"," << endl;
  o << string(indent + 1, ' ') << "state : " << p.state << "," << endl;
  o << string(indent + 1, ' ') << "args : " << "[";
  for (unsigned int i = 1; i < p.args.size(); i++) {
    o << "\"" << escapeJsonString(p.args[i]) <<"\", ";
  }
  o << "]," << endl;

  o << string(indent + 1, ' ') << "env : " << "[";
  for (auto it = p.env_vars.begin(); it != p.env_vars.end(); ++it) {
    o << "\"" << escapeJsonString(*it) << "\",";
  }
  o << "]," << endl;

  o << string(indent + 1, ' ') << "libs : " << "[";
  for (auto it = p.libs.begin(); it != p.libs.end(); ++it) {
    o << "\"" << *it << "\",";
  }
  o << "]," << endl;

  switch (p.state) {
  case FB_PROC_FINISHED: {
    o << string(indent + 1, ' ') << "exit_status : " << p.stime_m << "," << endl;
    // break; is missing intentionally
  }
  case FB_PROC_EXECED: {
    o << string(indent + 1, ' ') << "utime_m : " << p.utime_m << "," << endl;
    o << string(indent + 1, ' ') << "stime_m : " << p.stime_m << "," << endl;
    o << string(indent + 1, ' ') << "sum_utime_m : " << p.sum_utime_m << "," << endl;
    o << string(indent + 1, ' ') << "sum_stime_m : " << p.sum_stime_m << "," << endl;
    // break; is missing intentionally
  }
  case FB_PROC_RUNNING: {
    // something went wrong
  }
  }
}

}

