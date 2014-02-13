/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "ExecedProcess.h"

namespace firebuild {

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
    if (exec_parent_->type() == FB_PROC_EXEC_STARTED) {
      (dynamic_cast<ExecedProcess*>(exec_parent_))->
          propagate_exit_status(status);
    }
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

ExecedProcess::~ExecedProcess() {
  for (auto it = this->file_usages_.begin();
       it != this->file_usages_.end();
       ++it) {
    delete(it->second);
  }
}

}  // namespace firebuild
