/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#include "firebuild/ProcessTree.h"

#include <math.h>

#include <cstdio>
#include <stdexcept>
#include <limits>

#include "firebuild/Debug.h"

namespace firebuild {

ProcessTree::~ProcessTree() {
  // clean up all processes
  for (auto pair : fb_pid2proc_) {
    delete(pair.second);
  }
}

void ProcessTree::insert_process(Process *p, const int sock) {
  sock2proc_[sock] = p;
  fb_pid2proc_[p->fb_pid()] = p;
  pid2proc_[p->pid()] = p;
}

void ProcessTree::insert(Process *p, const int sock) {
  insert_process(p, sock);
}

void ProcessTree::insert(ExecedProcess *p, const int sock) {
  if (root_ == NULL) {
    root_ = p;
  } else if (NULL == p->exec_parent()) {
    // root's exec_parent is firebuild which is not in the tree.
    // If any other parent is missing, FireBuild missed process
    // that can happen due to the missing process(es) being statically built
    fb_error("TODO(rbalint) handle: Process without known exec parent\n");
  }

  insert_process(p, sock);
}

void ProcessTree::finished(const int sock) {
  Process *p = sock2proc_[sock];
  p->finish();
  sock2proc_.erase(sock);
}

void ProcessTree::export2js(FILE * stream) {
  fprintf(stream, "root = ");
  unsigned int nodeid = 0;
  root_->export2js_recurse(0, stream, &nodeid);
}

void ProcessTree::
profile_collect_cmds(const Process &p,
                     std::unordered_map<std::string, subcmd_prof> *cmds,
                     std::set<std::string> *ancestors) {
  if (p.exec_child() != NULL) {
    ExecedProcess *ec = static_cast<ExecedProcess*>(p.exec_child());
    if (0 == ancestors->count(ec->args()[0])) {
      (*cmds)[ec->args()[0]].sum_aggr_time += p.exec_child()->aggr_time();
    } else {
      if (!(*cmds)[ec->args()[0]].recursed) {
        (*cmds)[ec->args()[0]].recursed = true;
      }
    }
    (*cmds)[ec->args()[0]].count += 1;
  }
  for (auto child : p.children()) {
    profile_collect_cmds(*child, cmds, ancestors);
  }
}

void ProcessTree::build_profile(const Process &p,
                                std::set<std::string> *ancestors) {
  bool first_visited = false;
  if (p.exec_started()) {
    auto *e = static_cast<const ExecedProcess*>(&p);
    auto &cmd_prof = cmd_profs_[e->args()[0]];
    if (0 == ancestors->count(e->args()[0])) {
      cmd_prof.aggr_time += e->aggr_time();
      ancestors->insert(e->args()[0]);
      first_visited = true;
    }
    cmd_prof.cmd_time += e->sum_utime_u() +  e->sum_stime_u();
    profile_collect_cmds(p, &cmd_prof.subcmds, ancestors);
  }
  if (p.exec_child() != NULL) {
    build_profile(*p.exec_child(), ancestors);
  }
  for (auto child : p.children()) {
    build_profile(*child, ancestors);
  }

  if (first_visited) {
    ancestors->erase(static_cast<const ExecedProcess*>(&p)->args()[0]);
  }
}


/**
 * Convert HSL color to HSV color
 *
 * From http://ariya.blogspot.hu/2008/07/converting-between-hsl-and-hsv.html
 */
static void hsl_to_hsv(const double hh, const double ss, const double ll,
                       double *const h, double * const s, double * const v) {
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
  const double hsl_min[] = {2.0/3.0, 0.80, 0.25};  // blue
  const double hsl_max[] = {0.0, 1.0, 0.5};        // red
  const double r = p / 100;
  double hsl[3];
  double hsv[3];

  hsl[0] = hsl_min[0] + r * (hsl_max[0] - hsl_min[0]);
  hsl[1] = hsl_min[1] + r * (hsl_max[1] - hsl_min[1]);
  hsl[2] = hsl_min[2] + r * (hsl_max[2] - hsl_min[2]);
  hsl_to_hsv(hsl[0], hsl[1], hsl[2], &(hsv[0]), &(hsv[1]), &(hsv[2]));

  return (std::to_string(hsv[0]) + ", " + std::to_string(hsv[1]) + ", " +
          std::to_string(hsv[2]));
}

static double percent_of(const double val, const double of) {
  return (((of < std::numeric_limits<double>::epsilon()) &&
           (of > -std::numeric_limits<double>::epsilon()))?(0.0):
          (val * 100 / of));
}

void ProcessTree::export_profile2dot(FILE* stream) {
  std::set<std::string> cmd_chain;
  double min_penwidth = 1, max_penwidth = 8;
  int64_t build_time;

  // build profile
  build_profile(*root_, &cmd_chain);
  build_time = root_->aggr_time();

  // print it
  fprintf(stream, "digraph {\n");
  fprintf(stream, "graph [dpi=63, ranksep=0.25, rankdir=LR, "
          "bgcolor=transparent, fontname=Helvetica, fontsize=12, "
          "nodesep=0.125];\n"
          "node [fontname=Helvetica, fontsize=12, style=filled, height=0,"
          " width=0, shape=box, fontcolor=white];\n"
          "edge [fontname=Helvetica, fontsize=12]\n");

  for (auto pair : cmd_profs_) {
    fprintf(stream, "    \"%s\" [label=<<B>%s</B><BR/>", (pair.first).c_str(),
            (pair.first).c_str());
    fprintf(stream, "%.2lf%%<BR/>(%.2lf%%)>, color=\"%s\"]\n",
            percent_of(pair.second.aggr_time, build_time),
            percent_of(pair.second.cmd_time, build_time),
            pct_to_hsv_str(percent_of(pair.second.aggr_time,
                                      build_time)).c_str());
    for (auto pair2 : pair.second.subcmds) {
      fprintf(stream, "    \"%s\" -> \"%s\" [label=\"",
              (pair.first).c_str(), (pair2.first).c_str());
      if (!pair2.second.recursed) {
        fprintf(stream, "%.2lf%%\\n", percent_of(pair2.second.sum_aggr_time,
                                              build_time));
      }
      fprintf(stream, "×%lu\", color=\"%s\","
              " penwidth=\"%lf\"];",
              pair2.second.count,
              pct_to_hsv_str(percent_of(pair2.second.sum_aggr_time,
                                        build_time)).c_str(),
              (min_penwidth  + ((percent_of(pair2.second.sum_aggr_time,
                                            build_time) / 100)
                                * (max_penwidth - min_penwidth))));
    }
  }

  fprintf(stream, "}\n");
}

}  // namespace firebuild
