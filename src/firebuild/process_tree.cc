/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#include "firebuild/process_tree.h"

#include <math.h>

#include <cstdio>
#include <stdexcept>
#include <limits>

#include "firebuild/debug.h"
#include "firebuild/platform.h"

namespace firebuild {

ProcessTree::ProcessTree()
    : inherited_fds_(std::make_shared<std::vector<std::shared_ptr<FileFD>>>()),
      inherited_fd_pipes_(), fb_pid2proc_(), pid2proc_(),
      pid2fork_child_sock_(), pid2exec_child_sock_(), pid2posix_spawn_child_sock_(),
      cmd_profs_() {
  TRACK(FB_DEBUG_PROCTREE, "");

  // TODO(rbalint) support other inherited fds
  /* Create the FileFD representing stdin of the top process. */
  Process::add_filefd(inherited_fds_, STDIN_FILENO,
                      std::make_shared<FileFD>(STDIN_FILENO, O_RDONLY));

  /* Create the Pipes and FileFDs representing stdout and stderr of the top process. */
  // FIXME Make this more generic, for all the received pipes / terminal outputs.
  bool stdout_stderr_match = platform::fdcmp(STDOUT_FILENO, STDERR_FILENO) == 0;
  FB_DEBUG(FB_DEBUG_PROCTREE, stdout_stderr_match ? "Top level stdout and stderr are the same" :
           "Top level stdout and stderr are distinct");

  std::shared_ptr<Pipe> pipe;
  for (auto fd : {STDOUT_FILENO, STDERR_FILENO}) {
    if (fd == STDERR_FILENO && stdout_stderr_match) {
      /* stdout and stderr point to the same location (changing one's flags did change the
       * other's). Reuse the Pipe object that we created in the loop's first iteration. */
      pipe = (*inherited_fds_)[STDOUT_FILENO]->pipe();
    } else {
      /* Create a new Pipe for this file descriptor.
       * The fd keeps blocking/non-blocking behaviour, it seems to be ok with libevent.
       * The fd is dup()-ed first to let it be closed without closing the original fd. */
      int fd_dup = fcntl(fd, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
      assert(fd_dup != -1);
#ifdef __clang_analyzer__
      /* Scan-build reports a false leak for the correct code. This is used only in static
       * analysis. It is broken because all shared pointers to the Pipe must be copies of
       * the shared self pointer stored in it. */
      pipe = std::make_shared<Pipe>(fd_dup, nullptr);
#else
      pipe = (new Pipe(fd_dup, nullptr))->shared_ptr();
#endif
      FB_DEBUG(FB_DEBUG_PIPE, "created pipe with fd0: " + d(fd) + ", dup()-ed as: " + d(fd_dup));
      /* Top level inherited fds are special, they should not be closed. */
      inherited_fd_pipes_.insert(pipe);
    }

    std::shared_ptr<FileFD> file_fd =
        Process::add_filefd(inherited_fds_, fd, std::make_shared<FileFD>(fd, O_WRONLY));
    file_fd->set_pipe(pipe);
  }
}

ProcessTree::~ProcessTree() {
  TRACK(FB_DEBUG_PROCTREE, "");

  // clean up all processes, from the leaves towards the root
  delete_process_subtree(root());
  // clean up pending exec() children
  for (auto& pair : pid2exec_child_sock_) {
    delete(pair.second.incomplete_child);
  }
  // clean up pending posix_spawn() children
  for (auto& pair : pid2posix_spawn_child_sock_) {
    delete(pair.second.incomplete_child);
  }
}

void ProcessTree::delete_process_subtree(Process *p) {
  if (!p) {
    return;
  }
  delete_process_subtree(p->exec_child());
  for (Process *fork_child : p->fork_children()) {
    delete_process_subtree(fork_child);
  }
  delete p;
}

void ProcessTree::insert_process(Process *p) {
  TRACK(FB_DEBUG_PROCTREE, "p=%s", D(p));

  fb_pid2proc_[p->fb_pid()] = p;
  pid2proc_[p->pid()] = p;
}

void ProcessTree::insert(Process *p) {
  TRACK(FB_DEBUG_PROCTREE, "p=%s", D(p));

  insert_process(p);
}

void ProcessTree::insert(ExecedProcess *p) {
  TRACK(FB_DEBUG_PROCTREE, "p=%s", D(p));

  if (root_ == NULL) {
    root_ = p;
  } else if (p->parent() == NULL) {
    // root's parent is firebuild which is not in the tree.
    // If any other parent is missing, FireBuild missed process
    // that can happen due to the missing process(es) being statically built
    fb_error("TODO(rbalint) handle: Process without known exec parent\n");
  }

  insert_process(p);
}

void ProcessTree::export2js(FILE * stream) {
  fprintf(stream, "data = ");
  unsigned int nodeid = 0;
  root_->export2js_recurse(0, stream, &nodeid);
}

void ProcessTree::
profile_collect_cmds(const Process &p,
                     std::unordered_map<std::string, subcmd_prof> *cmds,
                     std::set<std::string> *ancestors) {
  if (p.exec_child() != NULL) {
    ExecedProcess *ec = static_cast<ExecedProcess*>(p.exec_child());
    if (ancestors->count(ec->args()[0]) == 0) {
      (*cmds)[ec->args()[0]].sum_aggr_time += ec->aggr_cpu_time_u();
    } else {
      if (!(*cmds)[ec->args()[0]].recursed) {
        (*cmds)[ec->args()[0]].recursed = true;
      }
    }
    (*cmds)[ec->args()[0]].count += 1;
  }
  for (auto& fork_child : p.fork_children()) {
    profile_collect_cmds(*fork_child, cmds, ancestors);
  }
}

void ProcessTree::build_profile(const Process &p,
                                std::set<std::string> *ancestors) {
  bool first_visited = false;
  if (p.exec_started()) {
    auto *e = static_cast<const ExecedProcess*>(&p);
    auto &cmd_prof = cmd_profs_[e->args()[0]];
    if (ancestors->count(e->args()[0]) == 0) {
      cmd_prof.aggr_time += e->aggr_cpu_time_u();
      ancestors->insert(e->args()[0]);
      first_visited = true;
    }
    cmd_prof.cmd_time += e->utime_u() +  e->stime_u();
    profile_collect_cmds(p, &cmd_prof.subcmds, ancestors);
  }
  if (p.exec_child() != NULL) {
    build_profile(*p.exec_child(), ancestors);
  }
  for (auto& fork_child : p.fork_children()) {
    build_profile(*fork_child, ancestors);
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
           (of > -std::numeric_limits<double>::epsilon())) ? 0.0 :
          val * 100 / of);
}

void ProcessTree::export_profile2dot(FILE* stream) {
  std::set<std::string> cmd_chain;
  double min_penwidth = 1, max_penwidth = 8;
  int64_t build_time;

  // build profile
  build_profile(*root_, &cmd_chain);
  build_time = root_->aggr_cpu_time_u();

  // print it
  fprintf(stream, "digraph {\n");
  fprintf(stream, "graph [dpi=63, ranksep=0.25, rankdir=LR, "
          "bgcolor=transparent, fontname=Helvetica, fontsize=12, "
          "nodesep=0.125];\n"
          "node [fontname=Helvetica, fontsize=12, style=filled, height=0,"
          " width=0, shape=box, fontcolor=white];\n"
          "edge [fontname=Helvetica, fontsize=12]\n");

  for (auto& pair : cmd_profs_) {
    fprintf(stream, "    \"%s\" [label=<<B>%s</B><BR/>", pair.first.c_str(),
            pair.first.c_str());
    fprintf(stream, "%.2lf%%<BR/>(%.2lf%%)>, color=\"%s\"]\n",
            percent_of(pair.second.aggr_time, build_time),
            percent_of(pair.second.cmd_time, build_time),
            pct_to_hsv_str(percent_of(pair.second.aggr_time,
                                      build_time)).c_str());
    for (auto& pair2 : pair.second.subcmds) {
      fprintf(stream, "    \"%s\" -> \"%s\" [label=\"",
              pair.first.c_str(), pair2.first.c_str());
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
