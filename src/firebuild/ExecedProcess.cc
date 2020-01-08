/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/ExecedProcess.h"

#include <map>
#include <sstream>

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

ExecedProcess::ExecedProcess(const int pid, const int ppid,
                             const std::string &cwd,
                             const std::string &executable,
                             Process * exec_parent)
    : Process(pid, ppid, cwd, exec_parent), can_shortcut_(true),
      exec_parent_(exec_parent), sum_utime_u_(0), sum_stime_u_(0), cwd_(cwd),
      wds_(), failed_wds_(), args_(), env_vars_(), executable_(executable),
      libs_(), file_usages_() {
  if (NULL != exec_parent) {
    // add as exec child of parent
    exec_parent->set_exec_child(this);
    exec_parent->set_state(FB_PROC_EXECED);
  }
}

void ExecedProcess::propagate_exit_status(const int status) {
  if (exec_parent_) {
    exec_parent_->set_exit_status(status);
    exec_parent_->set_state(FB_PROC_FINISHED);
    exec_parent_->propagate_exit_status(status);
  }
}

void ExecedProcess::exit_result(const int status, const int64_t utime_u,
                                const int64_t stime_u) {
  // store results for this process
  Process::exit_result(status, utime_u, stime_u);
  // propagate to parents exec()-ed this FireBuild process
  propagate_exit_status(status);
  // store data for shortcutting
  if (can_shortcut()) {
    // TODO(rbalint) store data
  }
}

int64_t ExecedProcess::sum_rusage_recurse() {
  int64_t aggr_time = utime_u() + stime_u();
  sum_utime_u_ = 0;
  sum_stime_u_ = 0;
  sum_rusage(&sum_utime_u_, &sum_stime_u_);
  if (exec_parent_ && exec_parent_->pid() == pid()) {
    sum_utime_u_ -= exec_parent_->utime_u();
    sum_stime_u_ -= exec_parent_->stime_u();
    aggr_time -= exec_parent_->utime_u();
    aggr_time -= exec_parent_->stime_u();
  }
  set_aggr_time(aggr_time);
  return Process::sum_rusage_recurse();
}

void ExecedProcess::export2js_recurse(const unsigned int level, FILE* stream,
                                      unsigned int *nodeid) {
  if (level > 0) {
    fprintf(stream, "\n");
  }
  fprintf(stream, "%s{", std::string(2 * level, ' ').c_str());

  export2js(level, stream, nodeid);
  fprintf(stream, "%s children: [", std::string(2 * level, ' ').c_str());
  Process::export2js_recurse(level, stream, nodeid);
  if (level == 0) {
    fprintf(stream, "]};\n");
  } else {
    fprintf(stream, "]},\n");
  }
}


void ExecedProcess::export2js(const unsigned int level,
                              FILE* stream, unsigned int * nodeid) {
  // TODO(rbalint): escape all strings properly
  auto indent_str = std::string(2 * level, ' ');
  const char* indent = indent_str.c_str();

  fprintf(stream, "name:\"%s\",\n", args()[0].c_str());
  fprintf(stream, "%s id: %u,\n", indent, (*nodeid)++);
  fprintf(stream, "%s pid: %u,\n", indent, pid());
  fprintf(stream, "%s ppid: %u,\n", indent, ppid());
  fprintf(stream, "%s fb_pid: %u,\n", indent, fb_pid());
  fprintf(stream, "%s cwd:\"%s\",\n", indent, cwd().c_str());
  fprintf(stream, "%s exe:\"%s\",\n", indent, executable().c_str());
  fprintf(stream, "%s state: %u,\n", indent, state());
  if (!can_shortcut_) {
    fprintf(stream, "%s cant_sc_reason: \"%s\",\n", indent, cant_shortcut_reason_.c_str());
    if (cant_shortcut_proc_->exec_proc()->fb_pid() != fb_pid()) {
      fprintf(stream, "%s cant_sc_fb_pid: \"%u\",\n", indent, cant_shortcut_proc_->exec_proc()->fb_pid());
    }
  }
  fprintf(stream, "%s args: [", indent);
  for (auto arg : args()) {
    fprintf(stream, "\"%s\",", escapeJsonString(arg).c_str());
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s env: [", indent);
  for (auto env : env_vars()) {
    fprintf(stream, "\"%s\",", escapeJsonString(env).c_str());
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s libs: [", indent);
  for (auto lib : libs()) {
    fprintf(stream, "\"%s\",", lib.c_str());
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s wds: [", indent);
  for (auto wd : wds()) {
    fprintf(stream, "\"%s\",", wd.c_str());
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s failed_wds: [", indent);
  for (auto f_wd : failed_wds()) {
    fprintf(stream, "\"%s\",", f_wd.c_str());
  }
  fprintf(stream, "],\n");

  // sort files before printing
  std::map<std::string, FileUsage*> ordered_file_usages;
  for (auto pair : file_usages()) {
    ordered_file_usages[pair.first] =  pair.second;
  }

  fprintf(stream, "%s fcreated: [", indent);
  for (auto pair : ordered_file_usages) {
    if (pair.second->created()) {
      fprintf(stream, "\"%s\",", pair.first.c_str());
    }
  }
  fprintf(stream, "],\n");

  // TODO(rbalint) replace write/read flag checks with more accurate tests
  fprintf(stream, "%s fmodified: [", indent);
  for (auto pair : ordered_file_usages) {
    if ((!pair.second->created()) &&
        (((pair.second->open_flags() & O_ACCMODE) == O_WRONLY) ||
         ((pair.second->open_flags() & O_ACCMODE) == O_RDWR))) {
      fprintf(stream, "\"%s\",", pair.first.c_str());
    }
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s fread: [", indent);
  for (auto pair : ordered_file_usages) {
    if (!pair.second->open_failed() &&
        (((pair.second->open_flags() & O_ACCMODE) == O_RDONLY) ||
         ((pair.second->open_flags() & O_ACCMODE) == O_RDWR))) {
      fprintf(stream, "\"%s\",", pair.first.c_str());
    }
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s fnotf: [", indent);
  for (auto pair : ordered_file_usages) {
    if (pair.second->open_failed()) {
      fprintf(stream, "\"%s\",", pair.first.c_str());
    }
  }
  fprintf(stream, "],\n");

  switch (state()) {
    case FB_PROC_FINISHED: {
      if (exit_status() != -1)
        fprintf(stream, "%s exit_status: %u,\n", indent, exit_status());
      __attribute__((fallthrough));
    }
    case FB_PROC_EXECED: {
      fprintf(stream, "%s utime_u: %lu,\n", indent, utime_u());
      fprintf(stream, "%s stime_u: %lu,\n", indent, stime_u());
      fprintf(stream, "%s aggr_time: %lu,\n", indent, aggr_time());
      fprintf(stream, "%s sum_utime_u: %lu,\n", indent, sum_utime_u());
      fprintf(stream, "%s sum_stime_u: %lu,\n", indent, sum_stime_u());
      __attribute__((fallthrough));
    }
    case FB_PROC_RUNNING: {
      // something went wrong
    }
  }
}

ExecedProcess::~ExecedProcess() {
  for (auto pair : file_usages()) {
    delete(pair.second);
  }
}

}  // namespace firebuild
