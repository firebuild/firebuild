/* Copyright (c) 2022 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include "firebuild/report.h"

#include <libgen.h>

#include <algorithm>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "firebuild/debug.h"
#include "firebuild/process_tree.h"

namespace firebuild {

/**
 * Profile is aggregated by command name (argv[0]).
 * For each command (C) we store the cumulated CPU time in microseconds
 * (system + user time), and count the invocations of each other command
 * by C. */
tsl::hopscotch_map<std::string, cmd_prof> cmd_profs {};

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

static const char* full_relative_path_or_basename(const char *name) {
  const char* name_last_slash = strrchr(name, '/');
  return name_last_slash && path_is_absolute(name) ? name_last_slash + 1 : name;
}

static void export2js(const ExecedProcess* proc, const unsigned int level,
                      FILE* stream, unsigned int * nodeid) {
  // TODO(rbalint): escape all strings properly
  auto indent_str = std::string(2 * level, ' ');
  const char* indent = indent_str.c_str();

  fprintf(stream, "name:\"%s\",\n", full_relative_path_or_basename(proc->args()[0].c_str()));
  fprintf(stream, "%s id: %u,\n", indent, (*nodeid)++);
  fprintf(stream, "%s pid: %u,\n", indent, proc->pid());
  fprintf(stream, "%s ppid: %u,\n", indent, proc->ppid());
  fprintf(stream, "%s fb_pid: %u,\n", indent, proc->fb_pid());
  fprintf(stream, "%s initial_wd:\"%s\",\n", indent, proc->initial_wd()->c_str());
  fprintf(stream, "%s exe:\"%s\",\n", indent, proc->executable()->c_str());
  fprintf(stream, "%s state: %u,\n", indent, proc->state());
  if (proc->was_shortcut()) {
    fprintf(stream, "%s was_shortcut: true,\n", indent);
  }
  if (!proc->can_shortcut()) {
    fprintf(stream, "%s cant_sc_reason: \"%s\",\n",
            indent, escapeJsonString(proc->cant_shortcut_reason()).c_str());
    if (proc->cant_shortcut_proc()->exec_proc()->fb_pid() != proc->fb_pid()) {
      fprintf(stream, "%s cant_sc_fb_pid: \"%u\",\n",
              indent, proc->cant_shortcut_proc()->exec_proc()->fb_pid());
    }
  }
  fprintf(stream, "%s args: [", indent);
  for (auto& arg : proc->args()) {
    fprintf(stream, "\"%s\",", escapeJsonString(arg).c_str());
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s env: [", indent);
  for (auto& env : proc->env_vars()) {
    fprintf(stream, "\"%s\",", escapeJsonString(env).c_str());
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s libs: [", indent);
  for (auto& lib : proc->libs()) {
    fprintf(stream, "\"%s\",", lib->c_str());
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s wds: [", indent);
  for (auto& wd : proc->wds()) {
    fprintf(stream, "\"%s\",", wd->c_str());
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s failed_wds: [", indent);
  for (auto& f_wd : proc->failed_wds()) {
    fprintf(stream, "\"%s\",", f_wd->c_str());
  }
  fprintf(stream, "],\n");

  /* sort files before printing */
  std::vector<file_file_usage> ordered_file_usages;
  for (auto& pair : proc->file_usages()) {
    ordered_file_usages.push_back({pair.first, pair.second});
  }
  std::sort(ordered_file_usages.begin(), ordered_file_usages.end(), file_file_usage_cmp);

  fprintf(stream, "%s fcreated: [", indent);
  for (auto& ffu : ordered_file_usages) {
    bool isreg_with_hash = ffu.usage->initial_type() == ISREG && ffu.usage->initial_hash_known();
    if (!isreg_with_hash && ffu.usage->written()) {
      fprintf(stream, "\"%s\",", ffu.file->c_str());
    }
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s fmodified: [", indent);
  for (auto& ffu : ordered_file_usages) {
    bool isreg_with_hash = ffu.usage->initial_type() == ISREG && ffu.usage->initial_hash_known();
    if (isreg_with_hash && ffu.usage->written()) {
      fprintf(stream, "\"%s\",", ffu.file->c_str());
    }
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s fread: [", indent);
  for (auto& ffu : ordered_file_usages) {
    bool isreg_with_hash = ffu.usage->initial_type() == ISREG && ffu.usage->initial_hash_known();
    if (isreg_with_hash && !ffu.usage->written()) {
      fprintf(stream, "\"%s\",", ffu.file->c_str());
    }
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s fnotf: [", indent);
  for (auto& ffu : ordered_file_usages) {
    if (ffu.usage->initial_type() == NOTEXIST) {
      fprintf(stream, "\"%s\",", ffu.file->c_str());
    }
  }
  fprintf(stream, "],\n");

  if (proc->state() != FB_PROC_FINALIZED) {
    // TODO(rbalint) something went wrong
  }
  if (proc->fork_point()->exit_status() != -1) {
    fprintf(stream, "%s exit_status: %u,\n", indent, proc->fork_point()->exit_status());
  }
  fprintf(stream, "%s utime_u: %lu,\n", indent, proc->utime_u());
  fprintf(stream, "%s stime_u: %lu,\n", indent, proc->stime_u());
  fprintf(stream, "%s aggr_time: %lu,\n", indent, proc->aggr_cpu_time_u());
}

static void export2js_recurse_ep(const ExecedProcess* proc, const unsigned int level, FILE* stream,
                                 unsigned int *nodeid);

static void export2js_recurse_p(const Process* proc, const unsigned int level, FILE* stream,
                              unsigned int *nodeid) {
  if (proc->exec_child() != NULL) {
    export2js_recurse_ep(proc->exec_child(), level + 1, stream, nodeid);
  }
  for (const Process* fork_child : proc->fork_children()) {
    export2js_recurse_p(fork_child, level, stream, nodeid);
  }
}

static void export2js_recurse_ep(const ExecedProcess* proc, const unsigned int level, FILE* stream,
                              unsigned int *nodeid) {
  if (level > 0) {
    fprintf(stream, "\n");
  }
  fprintf(stream, "%s{", std::string(2 * level, ' ').c_str());

  export2js(proc, level, stream, nodeid);
  fprintf(stream, "%s children: [", std::string(2 * level, ' ').c_str());
  export2js_recurse_p(proc, level, stream, nodeid);
  if (level == 0) {
    fprintf(stream, "]};\n");
  } else {
    fprintf(stream, "]},\n");
  }
}

static void export2js(ProcessTree* proc_tree, FILE * stream) {
  fprintf(stream, "data = ");
  unsigned int nodeid = 0;
  if (proc_tree->root()->exec_child()) {
    export2js_recurse_ep(proc_tree->root()->exec_child(), 0, stream, &nodeid);
  } else {
    // TODO(rbalint) provide nicer report on this error
    fprintf(stream, "{name: \"<unknown>\", id: 0, aggr_time: 0, children: []};");
  }
}

static void profile_collect_cmds(const Process &p,
                                 tsl::hopscotch_map<std::string, subcmd_prof> *cmds,
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

static void build_profile(const Process &p,
                          std::set<std::string> *ancestors) {
  bool first_visited = false;
  if (p.exec_started()) {
    auto *e = static_cast<const ExecedProcess*>(&p);
    auto &cmd_prof = cmd_profs[e->args()[0]];
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
  const double hsl_min[] = {2.0/3.0, 0.80, 0.25};  /* blue */
  const double hsl_max[] = {0.0, 1.0, 0.5};        /* red */
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

static void export_profile2dot(FILE* stream) {
  std::set<std::string> cmd_chain;
  double min_penwidth = 1, max_penwidth = 8;
  int64_t build_time;

  /* build profile */
  build_profile(*proc_tree->root(), &cmd_chain);
  build_time = (proc_tree->root() && proc_tree->root()->exec_child()) ?
      proc_tree->root()->exec_child()->aggr_cpu_time_u() : 0;

  /* print it */
  fprintf(stream, "digraph {\n");
  fprintf(stream, "graph [dpi=63, ranksep=0.25, rankdir=LR, "
          "bgcolor=transparent, fontname=Helvetica, fontsize=12, "
          "nodesep=0.125];\n"
          "node [fontname=Helvetica, fontsize=12, style=filled, height=0,"
          " width=0, shape=box, fontcolor=white];\n"
          "edge [fontname=Helvetica, fontsize=12]\n");

  for (auto& pair : cmd_profs) {
    fprintf(stream, "    \"%s\" [label=<<B>%s</B><BR/>", pair.first.c_str(),
            full_relative_path_or_basename(pair.first.c_str()));
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

/**
 * Copy whole file content from in_fd to out_fd retrying on temporary problems.
 * @param out_fd file desctiptor to write content to
 * @param in_fd file desctiptor to read content from
 * @return bytes written, -1 on error
 */
static ssize_t sendfile_full(int out_fd, int in_fd) {
  char buf[4096];
  ssize_t nread, ret = 0;

  while (nread = read(in_fd, buf, sizeof buf), nread > 0) {
    char *out_ptr = buf;
    ssize_t nwritten;

    do {
      nwritten = write(out_fd, out_ptr, nread);

      if (nwritten >= 0)      {
        nread -= nwritten;
        out_ptr += nwritten;
        ret += nwritten;
      } else if (errno != EINTR)      {
        return -1;
      }
    } while (nread > 0);
  }
  return ret;
}

/*
 * TODO(rbalint) error handling
 */
void Report::write(const std::string &html_filename, const std::string &datadir) {
  const char dot_filename[] = "firebuild-profile.dot";
  const char svg_filename[] = "firebuild-profile.svg";
  // FIXME Use a search path, according to the locations in various popular distributions
  const std::string d3_datadir = "/usr/share/nodejs/d3/dist";
  const char d3_filename[] = "d3.min.js";
  const char tree_filename[] = "firebuild-process-tree.js";
  const char html_orig_filename[] = "build-report.html";
  const std::string dot_cmd = "dot";

  FILE* src_file = fopen((datadir + "/" + html_orig_filename).c_str(), "r");
  if (src_file == NULL) {
    fb_perror("fopen");
    fb_error("Opening file " + (datadir + "/" + html_orig_filename) +
                        " failed.");
    fb_error("Can not write build report.");
    return;
  }

  // dirname may modify its parameter thus we provide a writable char string
  char *html_filename_tmp = new char[html_filename.size() + 1];
  strncpy(html_filename_tmp, html_filename.c_str(), html_filename.size() + 1);
  std::string dir = dirname(html_filename_tmp);
  delete[] html_filename_tmp;

  /* export profile */
  {
    FILE* dot = fopen((dir + "/" + dot_filename).c_str(), "w");
    if (dot == NULL) {
      fb_perror("fopen");
      fb_error("Failed to open dot file for writing profile graph.");
    }
    export_profile2dot(dot);
    fclose(dot);
  }

  auto system_cmd =
      dot_cmd + " -Tsvg " + dir + "/" + dot_filename
      + " | sed 's/viewBox=\\\"[^\\\"]*\\\" //' > " + dir + "/" + svg_filename;
  if (system(system_cmd.c_str()) != 0) {
    fb_perror("system");
    fb_error("Failed to generate profile graph with the following command: "
                        + system_cmd);
  }

  FILE* dst_file = fopen(html_filename.c_str(), "w");
  int ret = dst_file == NULL ? -1 : 0;
  while ((ret != -1)) {
    char* line = NULL;
    size_t zero = 0;
    if (getline(&line, &zero, src_file) == -1) {
      /* finished reading file */
      if (!feof(src_file)) {
        fb_perror("getline");
        fb_error("Reading from report template failed.");
      }
      free(line);
      break;
    }
    if (strstr(line, d3_filename) != NULL) {
      int d3 = open((d3_datadir + "/" + d3_filename).c_str(), O_RDONLY);
      if (d3 == -1) {
        /* File is not available locally, use the online version. */
        fprintf(dst_file, "<script type=\"text/javascript\" "
                "src=\"https://firebuild.io/d3.v5.min.js\"></script>\n");
        fflush(dst_file);
      } else {
        fprintf(dst_file, "<script type=\"text/javascript\">\n");
        fflush(dst_file);
        ret = sendfile_full(fileno(dst_file), d3);
        fsync(fileno(dst_file));
        fprintf(dst_file, "    </script>\n");
        close(d3);
      }
    } else if (strstr(line, tree_filename) != NULL) {
      fprintf(dst_file, "<script type=\"text/javascript\">\n");
      export2js(proc_tree, dst_file);
      fprintf(dst_file, "    </script>\n");
    } else if (strstr(line, svg_filename) != NULL) {
      int svg = open((dir + "/" + svg_filename).c_str(), O_RDONLY);
      fflush(dst_file);
      ret = sendfile_full(fileno(dst_file), svg);
      fsync(fileno(dst_file));
      close(svg);
    } else {
      fprintf(dst_file, "%s", line);
    }
    free(line);
  }
  fclose(src_file);
  fclose(dst_file);
  std::cout << "FIREBUILD: Generated report: " + html_filename << std::endl;
}

}  /* namespace firebuild */

