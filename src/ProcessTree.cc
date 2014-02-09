#include "ProcessTree.h"

#include <math.h>

#include <cstdio>
#include <stdexcept>
#include <sstream>
#include <limits>

#include "Debug.h"

namespace firebuild {
  

/**
 * Escape std::string for JavaScript
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
  for (auto it = fb_pid2proc_.begin(); it != fb_pid2proc_.end(); ++it) {
    delete(it->second);
  }
}

void ProcessTree::insert (Process *p, const int sock)
{
  sock2proc_[sock] = p;
  fb_pid2proc_[p->fb_pid()] = p;
  pid2proc_[p->pid()] = p;
}

void ProcessTree::insert (ExecedProcess *p, const int sock)
{
  if (root_ == NULL) {
    root_ = p;
  } else {
    // add as exec child of parent
    try {
      p->set_exec_parent(pid2proc_.at(p->pid()));
      p->exec_parent()->set_exec_child(p);
      p->exec_parent()->set_state(FB_PROC_EXECED);
    } catch (const std::out_of_range& oor) {
      // root's exec_parent is firebuild which is not in the tree.
      // If any other parent is missing, FireBuild missed process
      // that can happen due to the missing process(es) being statically built
      fb_error("TODO handle: Process without known exec parent\n");
    }
  }

  this->insert(dynamic_cast<Process*>(p), sock);
}

void ProcessTree::insert (ForkedProcess *p, const int sock)
{

  // add as fork child of parent
  if (p->fork_parent()) {
    p->fork_parent()->children().push_back(p);
  }

  this->insert(dynamic_cast<Process*>(p), sock);
}

void ProcessTree::exit (Process *p, const int sock)
{
  (void)p;
  // TODO maybe this is not needed
  sock2proc_.erase(sock);
}

long int ProcessTree::sum_rusage_recurse(Process *p)
{
  long int aggr_time = p->utime_m() + p->stime_m();
  if (p->type() == FB_PROC_EXEC_STARTED) {
    auto e = dynamic_cast<ExecedProcess*>(p);
    long int sum_utime_m = 0;
    long int sum_stime_m = 0;
    e->sum_rusage(&sum_utime_m,
                  &sum_stime_m);
    if (e->exec_parent()) {
      e->set_sum_utime_m(sum_utime_m - e->exec_parent()->utime_m());
      e->set_sum_stime_m(sum_stime_m - e->exec_parent()->stime_m());

      aggr_time -= e->exec_parent()->utime_m();
      aggr_time -= e->exec_parent()->stime_m();
    } else {
      e->set_sum_utime_m(sum_utime_m);
      e->set_sum_stime_m(sum_stime_m);
    }
  }
  if (p->exec_child() != NULL) {
    aggr_time += sum_rusage_recurse(p->exec_child());
  }
  for (unsigned int i = 0; i < p->children().size(); i++) {
    aggr_time += sum_rusage_recurse(p->children()[i]);
  }
  p->set_aggr_time(aggr_time);
  return aggr_time;
}

void ProcessTree::export2js_recurse(const Process &p, const unsigned int level, FILE* stream, unsigned int *nodeid)
{
  if (p.type() == FB_PROC_EXEC_STARTED) {
    if (level > 0) {
      fprintf(stream,"\n");
    }
    fprintf(stream,"%s{", std::string(2 * level, ' ').c_str());

    export2js((ExecedProcess&)p, level, stream, nodeid);
    fprintf(stream,"%s children: [", std::string(2 * level, ' ').c_str());
  }
  if (p.exec_child() != NULL) {
    export2js_recurse(*p.exec_child(), level + 1, stream, nodeid);
  }
  for (unsigned int i = 0; i < p.children().size(); i++) {
    export2js_recurse(*p.children()[i], level, stream, nodeid);
  }
  if (p.type() == FB_PROC_EXEC_STARTED) {
    if (level == 0) {
      fprintf(stream,"]};\n");
    } else {
      fprintf(stream,"]},\n");
    }
  }
}

void ProcessTree::export2js(FILE * stream)
{
  fprintf(stream, "root = ");
  unsigned int nodeid = 0;
  export2js_recurse(*root_, 0, stream, &nodeid);
}

void ProcessTree::export2js(const ExecedProcess &p, const unsigned int level, FILE* stream, unsigned int * nodeid)
{
  // TODO: escape all strings properly
  auto indent_str = std::string(2 * level, ' ');
  const char* indent = indent_str.c_str();

  fprintf(stream, "name:\"%s\",\n", p.args()[0].c_str());
  fprintf(stream, "%s id: %u,\n", indent, (*nodeid)++);
  fprintf(stream, "%s pid: %u,\n", indent, p.pid());
  fprintf(stream, "%s ppid: %u,\n", indent, p.ppid());
  fprintf(stream, "%s cwd:\"%s\",\n", indent, p.cwd().c_str());
  fprintf(stream, "%s exe:\"%s\",\n", indent, p.executable().c_str());
  fprintf(stream, "%s state: %u,\n", indent, p.state());
  fprintf(stream, "%s args: [", indent);
  for (unsigned int i = 1; i < p.args().size(); i++) {
    fprintf(stream, "\"%s\",", escapeJsonString(p.args()[i]).c_str());
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s env: [", indent);
  for (auto it = p.env_vars().begin(); it != p.env_vars().end(); ++it) {
    fprintf(stream, "\"%s\",", escapeJsonString(*it).c_str());
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s libs: [", indent);
  for (auto it = p.libs().begin(); it != p.libs().end(); ++it) {
    fprintf(stream, "\"%s\",", (*it).c_str());
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s wds: [", indent);
  for (auto it = p.wds().begin(); it != p.wds().end(); ++it) {
    fprintf(stream, "\"%s\",", (*it).c_str());
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s failed_wds: [", indent);
  for (auto it = p.failed_wds().begin(); it != p.failed_wds().end(); ++it) {
    fprintf(stream, "\"%s\",", (*it).c_str());
  }
  fprintf(stream, "],\n");

  // sort files before printing
  std::map<std::string, FileUsage*> ordered_file_usages;
  for (auto it = p.file_usages().begin(); it != p.file_usages().end(); ++it) {
    ordered_file_usages[it->first] =  it->second;
  }

  fprintf(stream, "%s fcreated: [", indent);
  for (auto it = ordered_file_usages.begin(); it !=ordered_file_usages.end(); ++it) {
    if (it->second->created()) {
      fprintf(stream, "\"%s\",", (it->first).c_str());
    }
  }
  fprintf(stream, "],\n");

  // TODO replace write/read flag checks with more accurate tests
  fprintf(stream, "%s fmodified: [", indent);
  for (auto it =ordered_file_usages.begin(); it !=ordered_file_usages.end(); ++it) {
    if ((!it->second->created()) && (it->second->open_flags() & (O_WRONLY | O_RDWR))) {
      fprintf(stream, "\"%s\",", (it->first).c_str());
    }
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s fread: [", indent);
  for (auto it =ordered_file_usages.begin(); it !=ordered_file_usages.end(); ++it) {
    if (it->second->open_flags() & (O_RDONLY | O_RDWR)) {
      fprintf(stream, "\"%s\",", (it->first).c_str());
    }
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s fnotf: [", indent);
  for (auto it =ordered_file_usages.begin(); it !=ordered_file_usages.end(); ++it) {
    if (it->second->open_failed()) {
      fprintf(stream, "\"%s\",", (it->first).c_str());
    }
  }
  fprintf(stream, "],\n");

  switch (p.state()) {
    case FB_PROC_FINISHED: {
      fprintf(stream, "%s exit_status: %u,\n", indent, p.exit_status());
      // break; is missing intentionally
    }
    case FB_PROC_EXECED: {
      fprintf(stream, "%s utime_m: %lu,\n", indent, p.utime_m());
      fprintf(stream, "%s stime_m: %lu,\n", indent, p.stime_m());
      fprintf(stream, "%s aggr_time: %lu,\n", indent, p.aggr_time());
      fprintf(stream, "%s sum_utime_m: %lu,\n", indent, p.sum_utime_m());
      fprintf(stream, "%s sum_stime_m: %lu,\n", indent, p.sum_stime_m());
      // break; is missing intentionally
    }
    case FB_PROC_RUNNING: {
      // something went wrong
    }
  }
}

void ProcessTree::profile_collect_cmds(const Process &p,
                                       std::unordered_map<std::string, subcmd_prof> *cmds,
                                       std::set<std::string> *ancestors)
{
  if (p.exec_child() != NULL) {
    ExecedProcess *ec = (ExecedProcess*)(p.exec_child());
    if (0 == ancestors->count(ec->args()[0])) {
      (*cmds)[ec->args()[0]].sum_aggr_time += p.exec_child()->aggr_time();
    } else {
      if (!(*cmds)[ec->args()[0]].recursed) {
        (*cmds)[ec->args()[0]].recursed = true;
      }
    }
    (*cmds)[ec->args()[0]].count += 1;
  }
  for (unsigned int i = 0; i < p.children().size(); i++) {
    profile_collect_cmds(*p.children()[i], cmds, ancestors);
  }

}

void ProcessTree::build_profile(const Process &p, std::set<std::string> *ancestors)
{
  bool first_visited = false;
  if (p.type() == FB_PROC_EXEC_STARTED) {
    ExecedProcess *e = (ExecedProcess*)&p;
    auto &cmd_prof = cmd_profs_[e->args()[0]];
    if (0 == ancestors->count(e->args()[0])) {
      cmd_prof.aggr_time += e->aggr_time();
      ancestors->insert(e->args()[0]);
      first_visited = true;
    }
    cmd_prof.cmd_time += e->sum_utime_m() +  e->sum_stime_m();
    profile_collect_cmds(p, &cmd_prof.subcmds, ancestors);
  }
  if (p.exec_child() != NULL) {
    build_profile(*p.exec_child(), ancestors);
  }
  for (unsigned int i = 0; i < p.children().size(); i++) {
    build_profile(*(p.children()[i]), ancestors);
  }

  if (first_visited) {
    ancestors->erase(((ExecedProcess*)&p)->args()[0]);
  }
}


/**
 * Convert HSL color to HSV color
 *
 * From http://ariya.blogspot.hu/2008/07/converting-between-hsl-and-hsv.html
 */
static void hsl_to_hsv(const double hh, const double ss, const double ll,
                       double *const h, double * const s, double * const v)
{
  double ss_tmp;
  *h = hh;
  ss_tmp = ss * ((ll <= 0.5) ? ll : 1 - ll);
  *v = ll + ss_tmp;
  *s = (2 * ss_tmp) / (ll + ss_tmp);
}

/**
 * Ratio to HSL color std::string
 * @param r 0.0 .. 1.0
 */
static std::string pct_to_hsv_str(const double p) {
  const double hsl_min[] = {2.0/3.0, 0.80, 0.25}; // blue
  const double hsl_max[] = {0.0, 1.0, 0.5}; // red
  const double r = p / 100;
  double hsl[3];
  double hsv[3];

  hsl[0] = hsl_min[0] + r * (hsl_max[0] - hsl_min[0]);
  hsl[1] = hsl_min[1] + r * (hsl_max[1] - hsl_min[1]);
  hsl[2] = hsl_min[2] + r * (hsl_max[2] - hsl_min[2]);
  hsl_to_hsv(hsl[0], hsl[1], hsl[2], &(hsv[0]), &(hsv[1]), &(hsv[2]));

  return std::to_string(hsv[0]) + ", " + std::to_string(hsv[1]) + ", " + std::to_string(hsv[2]);
}

static double percent_of (const double val, const double of)
{
  return (((of < std::numeric_limits<double>::epsilon()) &&
           (of > -std::numeric_limits<double>::epsilon()))?(0.0):
          (round(val * 100 / of)));
}

void ProcessTree::export_profile2dot(FILE* stream)
{
  std::set<std::string> cmd_chain;
  double min_penwidth = 1, max_penwidth = 8;
  long int build_time;

  // build profile
  build_profile(*root_, &cmd_chain);
  build_time = root_->aggr_time();

  // print it
  fprintf(stream, "digraph {\n");
  fprintf(stream, "graph [dpi=63, ranksep=0.25, rankdir=LR, bgcolor=transparent,"
          " fontname=Helvetica, fontsize=12, nodesep=0.125];\n"
          "node [fontname=Helvetica, fontsize=12, style=filled, height=0,"
          " width=0, shape=box, fontcolor=white];\n"
          "edge [fontname=Helvetica, fontsize=12]\n");

  for (auto it = cmd_profs_.begin(); it != cmd_profs_.end(); ++it) {
    fprintf(stream, "    \"%s\" [label=<<B>%s</B><BR/>", (it->first).c_str(),
            (it->first).c_str());
    fprintf(stream, "%.2lf%%<BR/>(%.2lf%%)>, color=\"%s\"]\n",
            percent_of(it->second.aggr_time, build_time),
            percent_of(it->second.cmd_time, build_time),
            pct_to_hsv_str(percent_of(it->second.aggr_time,
                                      build_time)).c_str());
    for (auto it2 = it->second.subcmds.begin();
         it2 != it->second.subcmds.end(); ++it2) {
      fprintf(stream, "    \"%s\" -> \"%s\" [label=\"",
              (it->first).c_str(), (it2->first).c_str());
      if (!it2->second.recursed) {
        fprintf(stream, "%.2lf%%\\n", percent_of(it2->second.sum_aggr_time,
                                              build_time));
      }
      fprintf(stream, "×%lu\", color=\"%s\","
              " penwidth=\"%lf\"];",
              it2->second.count,
              pct_to_hsv_str(percent_of(it2->second.sum_aggr_time,
                                        build_time)).c_str(),
              (min_penwidth  + ((percent_of(it2->second.sum_aggr_time,
                                            build_time) / 100)
                                * (max_penwidth - min_penwidth))));
    }
  }

  fprintf(stream, "}\n");
}
}

