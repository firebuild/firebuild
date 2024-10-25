/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 *
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 * Modification and redistribution are permitted, but commercial use of derivative
 * works is subject to the same requirements of this license
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "firebuild/report.h"

#include <libgen.h>

#include <algorithm>
#include <cinttypes>
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
 * Index of each used file in the JavaScript files[] array.
 */
tsl::hopscotch_map<const FileName*, int> used_files_index_map {};


struct string_vector_ptr_hash {
  size_t operator()(const std::vector<std::string>* v) const {
    std::hash<std::string> hasher;
    std::size_t seed = 0;
    for (auto const & s : *v) {
      seed ^= hasher(s) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
    return seed;
  }
};

struct string_vector_ptr_eq {
  bool operator() (const std::vector<std::string>* a, const std::vector<std::string>* b) const {
    return *a == *b;
  }
};

/**
 * Index of each used environment in the JavaScript envs[] array.
 */
tsl::hopscotch_map<const std::vector<std::string>*, int, struct string_vector_ptr_hash,
                   struct string_vector_ptr_eq> used_envs_index_map {};

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

static void fprintf_ffu_file(FILE* stream, const file_file_usage& ffu) {
  fprintf(stream, "files[%d],", used_files_index_map[ffu.file]);
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
  if (proc->shortcut_result()) {
    fprintf(stream, "%s sc_result: \"%s\",\n",
            indent, escapeJsonString(proc->shortcut_result()).c_str());
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

  fprintf(stream, "%s env: envs[%d],\n", indent, used_envs_index_map[&proc->env_vars()]);

  fprintf(stream, "%s libs: [", indent);
  for (auto& lib : proc->libs()) {
    fprintf(stream, "files[%d],", used_files_index_map[lib]);
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
    if (!pair.second->propagated()) {
      ordered_file_usages.push_back({pair.first, pair.second});
    }
  }
  std::sort(ordered_file_usages.begin(), ordered_file_usages.end(), file_file_usage_cmp);

  fprintf(stream, "%s fcreated: [", indent);
  for (auto& ffu : ordered_file_usages) {
    bool isreg_with_hash = ffu.usage->initial_type() == ISREG && ffu.usage->initial_hash_known();
    if (!isreg_with_hash && ffu.usage->written()) {
      fprintf_ffu_file(stream, ffu);
    }
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s fmodified: [", indent);
  for (auto& ffu : ordered_file_usages) {
    bool isreg_with_hash = ffu.usage->initial_type() == ISREG && ffu.usage->initial_hash_known();
    if (isreg_with_hash && ffu.usage->written()) {
      fprintf_ffu_file(stream, ffu);
    }
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s fread: [", indent);
  for (auto& ffu : ordered_file_usages) {
    bool isreg_with_hash = ffu.usage->initial_type() == ISREG && ffu.usage->initial_hash_known();
    if (isreg_with_hash && !ffu.usage->written()) {
      fprintf_ffu_file(stream, ffu);
    }
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s fnotf: [", indent);
  for (auto& ffu : ordered_file_usages) {
    if (ffu.usage->initial_type() == NOTEXIST) {
      fprintf_ffu_file(stream, ffu);
    }
  }
  fprintf(stream, "],\n");

  if (proc->state() != FB_PROC_FINALIZED) {
    // TODO(rbalint) something went wrong
  }
  if (proc->fork_point()->exit_status() != -1) {
    fprintf(stream, "%s exit_status: %u,\n", indent, proc->fork_point()->exit_status());
  }
  fprintf(stream, "%s utime_u: %" PRId64 ",\n", indent, proc->utime_u());
  fprintf(stream, "%s stime_u: %" PRId64 ",\n", indent, proc->stime_u());
  fprintf(stream, "%s aggr_time_u: %" PRId64 ",\n", indent, proc->aggr_cpu_time_u());
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
    fprintf(stream, "{name: \"<unknown>\", id: 0, aggr_time_u: 0, children: []};");
  }
}

static void collect_used_files_and_envs(const Process &p,
                                        tsl::hopscotch_set<const FileName*> *used_files,
                                        tsl::hopscotch_set<const std::vector<std::string>*,
                                        string_vector_ptr_hash,
                                        string_vector_ptr_eq> *envs) {
  if (p.exec_child() != NULL) {
    ExecedProcess *exec_child = static_cast<ExecedProcess*>(p.exec_child());
    for (const auto& pair : exec_child->file_usages()) {
      if (!pair.second->propagated()) { /* Save time by not processing propagated ones. */
        used_files->insert(pair.first);
      }
    }
    for (const FileName* lib : exec_child->libs()) {
      used_files->insert(lib);
    }
    envs->insert(&exec_child->env_vars());
    collect_used_files_and_envs(*exec_child, used_files, envs);
  }
  for (auto& fork_child : p.fork_children()) {
    collect_used_files_and_envs(*fork_child, used_files, envs);
  }
}

void fprint_collected_files(FILE* stream,
                            const tsl::hopscotch_set<const FileName*>& used_files_set) {
  int index = 0;
  fprintf(stream, "files = [\n");
  for (const FileName* filename : used_files_set) {
    fprintf(stream, "  \"%s\", // files[%d]\n",
            escapeJsonString(filename->to_string()).c_str(), index);
    used_files_index_map.insert({filename, index++});
  }
  fprintf(stream, "];\n");
}

void fprint_collected_envs(
    FILE* stream, const tsl::hopscotch_set<const std::vector<std::string>*, string_vector_ptr_hash,
    string_vector_ptr_eq>& used_envs_set) {
  int index = 0;
  fprintf(stream, "envs = [\n");
  for (const std::vector<std::string>* env : used_envs_set) {
    fprintf(stream, "  [");
    for (const std::string& env_var : *env) {
      fprintf(stream, "\"%s\",", escapeJsonString(env_var).c_str());
    }
    fprintf(stream, "], // envs[%d]\n", index);
    used_envs_index_map.insert({env, index++});
  }
  fprintf(stream, "];\n");
}

static void profile_collect_cmds(const Process &p,
                                 tsl::hopscotch_map<std::string, subcmd_prof> *cmds,
                                 std::set<std::string> *ancestors) {
  if (p.exec_child() != NULL) {
    ExecedProcess *ec = static_cast<ExecedProcess*>(p.exec_child());
    if (ancestors->count(ec->args()[0]) == 0) {
      (*cmds)[ec->args()[0]].sum_aggr_time_u += ec->aggr_cpu_time_u();
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
      cmd_prof.aggr_time_u += e->aggr_cpu_time_u();
      ancestors->insert(e->args()[0]);
      first_visited = true;
    }
    cmd_prof.cmd_time_u += e->utime_u() +  e->stime_u();
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
            percent_of(pair.second.aggr_time_u, build_time),
            percent_of(pair.second.cmd_time_u, build_time),
            pct_to_hsv_str(percent_of(pair.second.aggr_time_u,
                                      build_time)).c_str());
    for (auto& pair2 : pair.second.subcmds) {
      fprintf(stream, "    \"%s\" -> \"%s\" [label=\"",
              pair.first.c_str(), pair2.first.c_str());
      if (!pair2.second.recursed) {
        fprintf(stream, "%.2lf%%\\n", percent_of(pair2.second.sum_aggr_time_u,
                                              build_time));
      }
      fprintf(stream, "×%" PRId64 "\", color=\"%s\","
              " penwidth=\"%lf\"];",
              pair2.second.count,
              pct_to_hsv_str(percent_of(pair2.second.sum_aggr_time_u,
                                        build_time)).c_str(),
              (min_penwidth  + ((percent_of(pair2.second.sum_aggr_time_u,
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
  // FIXME Use a search path, according to the locations in various popular distributions
  const std::string d3_datadir = "/usr/share/nodejs/d3/dist";
  const char d3_filename[] = "d3.min.js";
  const char tree_filename[] = "firebuild-process-tree.js";
  const char viz_js_filename[] = "viz-standalone.js";
  const char digraph_script[] = "id=\"digraph";
  const char html_orig_filename[] = "build-report.html";

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
        fprintf(dst_file, "    <script type=\"text/javascript\" "
                "src=\"https://firebuild.com/d3.v5.min.js\"></script>\n");
        fflush(dst_file);
      } else {
        fprintf(dst_file, "    <script type=\"text/javascript\">\n");
        fflush(dst_file);
        ret = sendfile_full(fileno(dst_file), d3);
        fsync(fileno(dst_file));
        fprintf(dst_file, "    </script>\n");
        close(d3);
      }
    } else if (strstr(line, viz_js_filename) != NULL) {
      // TODO(rbalint) check for local availability
      /* File is not available locally, use the online version. */
      fprintf(dst_file, "    <script type=\"text/javascript\" "
              "src=\"https://firebuild.com/viz-standalone.js\" id=\"viz-js\"></script>\n");
      fflush(dst_file);
    } else if (strstr(line, tree_filename) != NULL) {
      fprintf(dst_file, "    <script type=\"text/javascript\">\n");
      tsl::hopscotch_set<const FileName*> used_files_set;
      tsl::hopscotch_set<const std::vector<std::string>*, string_vector_ptr_hash,
                         string_vector_ptr_eq> used_envs_set;
      collect_used_files_and_envs(*proc_tree->root(), &used_files_set, &used_envs_set);
      fprint_collected_files(dst_file, used_files_set);
      fprint_collected_envs(dst_file, used_envs_set);
      export2js(proc_tree, dst_file);
      fprintf(dst_file, "    </script>\n");
    } else if (strstr(line, digraph_script) != NULL) {
      fprintf(dst_file, "%s", line);
      export_profile2dot(dst_file);
    } else {
      fprintf(dst_file, "%s", line);
    }
    free(line);
  }
  fclose(src_file);
  fclose(dst_file);
  fb_info("Generated report: " + html_filename);
}

}  /* namespace firebuild */
