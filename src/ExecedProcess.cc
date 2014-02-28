/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "ExecedProcess.h"

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
                             const std::string &executable)
    : Process(pid, ppid, FB_PROC_EXEC_STARTED, cwd),
      exec_parent_(NULL), sum_utime_m_(0), sum_stime_m_(0), cwd_(cwd),
      wds_(), failed_wds_(), args_(), env_vars_(), executable_(executable),
      libs_(), file_usages_() {
}

void ExecedProcess::propagate_exit_status(const int status) {
  if (exec_parent_) {
    exec_parent_->set_exit_status(status);
    exec_parent_->set_state(FB_PROC_FINISHED);
    exec_parent_->propagate_exit_status(status);
  }
}

void ExecedProcess::exit_result(const int status, const long int utime_m,
                                const long int stime_m) {
  // store results for this process
  Process::exit_result(status, utime_m, stime_m);
  set_state(FB_PROC_FINISHED);
  // propagate to parents exec()-ed this FireBuild process
  propagate_exit_status(status);
}

long int ExecedProcess::sum_rusage_recurse() {
  long int aggr_time = utime_m() + stime_m();
  sum_utime_m_ = 0;
  sum_stime_m_ = 0;
  sum_rusage(&sum_utime_m_, &sum_stime_m_);
  if (exec_parent_) {
    sum_utime_m_ -= exec_parent_->utime_m();
    sum_stime_m_ -= exec_parent_->stime_m();
    aggr_time -= exec_parent_->utime_m();
    aggr_time -= exec_parent_->stime_m();
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
  // TODO: escape all strings properly
  auto indent_str = std::string(2 * level, ' ');
  const char* indent = indent_str.c_str();

  fprintf(stream, "name:\"%s\",\n", args()[0].c_str());
  fprintf(stream, "%s id: %u,\n", indent, (*nodeid)++);
  fprintf(stream, "%s pid: %u,\n", indent, pid());
  fprintf(stream, "%s ppid: %u,\n", indent, ppid());
  fprintf(stream, "%s cwd:\"%s\",\n", indent, cwd().c_str());
  fprintf(stream, "%s exe:\"%s\",\n", indent, executable().c_str());
  fprintf(stream, "%s state: %u,\n", indent, state());
  fprintf(stream, "%s args: [", indent);
  for (unsigned int i = 1; i < args().size(); i++) {
    fprintf(stream, "\"%s\",", escapeJsonString(args()[i]).c_str());
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s env: [", indent);
  for (auto it = env_vars().begin(); it != env_vars().end(); ++it) {
    fprintf(stream, "\"%s\",", escapeJsonString(*it).c_str());
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s libs: [", indent);
  for (auto it = libs().begin(); it != libs().end(); ++it) {
    fprintf(stream, "\"%s\",", (*it).c_str());
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s wds: [", indent);
  for (auto it = wds().begin(); it != wds().end(); ++it) {
    fprintf(stream, "\"%s\",", (*it).c_str());
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s failed_wds: [", indent);
  for (auto it = failed_wds().begin(); it != failed_wds().end(); ++it) {
    fprintf(stream, "\"%s\",", (*it).c_str());
  }
  fprintf(stream, "],\n");

  // sort files before printing
  std::map<std::string, FileUsage*> ordered_file_usages;
  for (auto it = file_usages().begin(); it != file_usages().end(); ++it) {
    ordered_file_usages[it->first] =  it->second;
  }

  fprintf(stream, "%s fcreated: [", indent);
  for (auto it = ordered_file_usages.begin(); it != ordered_file_usages.end();
       ++it) {
    if (it->second->created()) {
      fprintf(stream, "\"%s\",", (it->first).c_str());
    }
  }
  fprintf(stream, "],\n");

  // TODO replace write/read flag checks with more accurate tests
  fprintf(stream, "%s fmodified: [", indent);
  for (auto it =ordered_file_usages.begin(); it != ordered_file_usages.end();
       ++it) {
    if ((!it->second->created()) &&
        (it->second->open_flags() & (O_WRONLY | O_RDWR))) {
      fprintf(stream, "\"%s\",", (it->first).c_str());
    }
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s fread: [", indent);
  for (auto it =ordered_file_usages.begin(); it !=ordered_file_usages.end();
       ++it) {
    if (it->second->open_flags() & (O_RDONLY | O_RDWR)) {
      fprintf(stream, "\"%s\",", (it->first).c_str());
    }
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s fnotf: [", indent);
  for (auto it =ordered_file_usages.begin(); it !=ordered_file_usages.end();
       ++it) {
    if (it->second->open_failed()) {
      fprintf(stream, "\"%s\",", (it->first).c_str());
    }
  }
  fprintf(stream, "],\n");

  switch (state()) {
    case FB_PROC_FINISHED: {
      fprintf(stream, "%s exit_status: %u,\n", indent, exit_status());
      // break; is missing intentionally
    }
    case FB_PROC_EXECED: {
      fprintf(stream, "%s utime_m: %lu,\n", indent, utime_m());
      fprintf(stream, "%s stime_m: %lu,\n", indent, stime_m());
      fprintf(stream, "%s aggr_time: %lu,\n", indent, aggr_time());
      fprintf(stream, "%s sum_utime_m: %lu,\n", indent, sum_utime_m());
      fprintf(stream, "%s sum_stime_m: %lu,\n", indent, sum_stime_m());
      // break; is missing intentionally
    }
    case FB_PROC_RUNNING: {
      // something went wrong
    }
  }
}

ExecedProcess::~ExecedProcess() {
  for (auto it = this->file_usages_.begin();
       it != this->file_usages_.end();
       ++it) {
    delete(it->second);
  }
}

}  // namespace firebuild
