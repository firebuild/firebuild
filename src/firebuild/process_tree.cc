/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#include "firebuild/process_tree.h"

#include <math.h>

#include <cstdio>
#include <stdexcept>
#include <limits>

#include "firebuild/debug.h"

namespace firebuild {

ProcessTree::ProcessTree(int top_pid)
    : top_pid_(top_pid), roots_(), inherited_fds_({}),
      inherited_fd_pipes_(), fb_pid2proc_(), pid2proc_(),
      pid2fork_child_sock_(), pid2exec_child_sock_(), pid2posix_spawn_child_sock_(),
      cmd_profs_() {
  TRACK(FB_DEBUG_PROCTREE, "");

  /* Create the Pipes and FileFDs representing stdout and stderr of the top process.
   * Check if stdout and stderr point to the same place. kcmp() is not universally
   * available, so do a back-n-forth fcntl() on one and see if it drags the other with it.
   * See https://unix.stackexchange.com/questions/191967.
   * Share the same Pipe object, or use two different Pipes depending on the outcome. */
  // FIXME Make this more generic, for all the received pipes / terminal outputs.
  // FIXME With shim support we can't safely toggle the fcntl flags as it might affect other
  // processes. Use kcmp() or /proc instead.
  int flags1 = fcntl(STDOUT_FILENO, F_GETFL);
  int flags2a = fcntl(STDERR_FILENO, F_GETFL);
  fcntl(STDOUT_FILENO, F_SETFL, flags1 ^ O_NONBLOCK);
  int flags2b = fcntl(STDERR_FILENO, F_GETFL);
  fcntl(STDOUT_FILENO, F_SETFL, flags1);
  FB_DEBUG(FB_DEBUG_PROCTREE, flags2a != flags2b ? "Top level stdout and stderr are the same" :
                                                   "Top level stdout and stderr are distinct");
  /* TODO(rbalint) pass proper access mode flags */
  int fds[] = {0, 1, 2};
  QueueShimFds(top_pid, fds, 3);
  inherit_fds(top_pid, flags2a != flags2b ? "0=0:1=1,2=1" : "0=0:1=1:2=1", true);
}

void ProcessTree::inherit_fds(int pid, const char* fds_string, bool keep_open) {
  assert(inherited_fds_.count(pid) == 0);
  // TODO(rbalint) support other inherited fds
  /* Create the FileFD representing stdin of the top process. */
  const std::vector<int>* fds = ShimPid2Fds(pid);
  inherited_fds_[pid] = std::make_shared<std::vector<std::shared_ptr<FileFD>>>();

  std::shared_ptr<Pipe> pipe;
  bool reuse_pipe = false;
  int scanf_ret, offset = 0;
  int i = -1;
  do {
    int fd, acc_mode, characters_read;
    char separator;
    scanf_ret = sscanf(&fds_string[offset], "%d=%d%c%n",
                       &fd, &acc_mode, &separator, &characters_read);
    offset += characters_read;
    i++;
    if (scanf_ret >= 2) {
      /* valid fd */
      if (fd == STDOUT_FILENO || fd == STDIN_FILENO || acc_mode == O_WRONLY) {
        /* to be captured */
        if (!reuse_pipe) {
          /* Create a new Pipe for this file descriptor.
           * The fd keeps blocking/non-blocking behaviour, it seems to be ok with libevent. */
#ifdef __clang_analyzer__
          /* Scan-build reports a false leak for the correct code. This is used only in static
           * analysis. It is broken because all shared pointers to the Pipe must be copies of
           * the shared self pointer stored in it. */
          pipe = std::make_shared<Pipe>((*fds)[i], nullptr);
#else
          pipe = (new Pipe((*fds)[i], nullptr))->shared_ptr();
#endif
          FB_DEBUG(FB_DEBUG_PIPE, "created pipe with fd0: " + d(fd));
          if (keep_open) {
            pipe->set_keep_fd0_open();
          }
          inherited_fd_pipes_.insert(pipe);
        } else if (!keep_open) {
          close((*fds)[i]);
        }

        std::shared_ptr<FileFD> file_fd =
            Process::add_filefd(inherited_fds_[pid], fd, std::make_shared<FileFD>(fd, acc_mode));
        file_fd->set_pipe(pipe);
      } else {
        close((*fds)[i]);
        continue;
      }
    } else {
      // TODO(rbalint) break close all, invalid string
    }
    if (scanf_ret == 3) {
      if (separator == ',') {
        reuse_pipe = true;
      } else {
        reuse_pipe = false;
      }
    }
  } while (scanf_ret == 3);
  DropShimFds(pid);
}

ProcessTree::~ProcessTree() {
  TRACK(FB_DEBUG_PROCTREE, "");

  // clean up all processes, from the leaves towards the root
  for (auto& pair : roots()) {
    delete_process_subtree(pair.second);
  }
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

  if (p->parent() == NULL) {
    int root_pid = p->shim_pid(p->env_vars());
    if (root_pid == -1) {
      root_pid = top_pid_;
    }
    assert(root_pid != -1);
    if (roots_.count(root_pid) == 0) {
      roots_[root_pid] = p;
    } else {
      // roots parent is firebuild which is not in the tree.
      // If any other parent is missing, FireBuild missed process
      // that can happen due to the missing process(es) being statically built
      fb_error("TODO(rbalint) handle: Process without known exec parent\n");
    }
  }
  insert_process(p);
}

void ProcessTree::export2js(FILE * stream) {
  fprintf(stream, "data = ");
  unsigned int nodeid = 0;

  if (roots().size() > 1) {
    int64_t utime_u = 0, stime_u = 0, aggr_cpu_time_u = 0;
    for (const auto& pair : roots()) {
      utime_u += pair.second->utime_u();
      stime_u += pair.second->stime_u();
      aggr_cpu_time_u += pair.second->aggr_cpu_time_u();
    }
    fprintf(stream, "{");
    fprintf(stream, "name:\"firebuild shims\",\n");
    fprintf(stream, "id: %u,\n", nodeid++);
    fprintf(stream, "utime_u: %lu,\n", utime_u);
    fprintf(stream, "stime_u: %lu,\n", stime_u);
    fprintf(stream, "aggr_time: %lu,\n", aggr_cpu_time_u);
    fprintf(stream, "children: [");
  }
  for (const auto& pair : roots()) {
    pair.second->export2js_recurse(1, stream, &nodeid);
  }
  if (roots().size() > 1) {
    fprintf(stream, "]};\n");
  }
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
  int64_t build_time = 0;

  // build profile
  for (auto& pair : roots()) {
    build_profile(*pair.second, &cmd_chain);
  }
  for (auto& pair : roots()) {
    build_time += pair.second->aggr_cpu_time_u();
  }

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
