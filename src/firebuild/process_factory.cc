/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/process_factory.h"

namespace firebuild {

class ExecedProcessCacher;

ForkedProcess* ProcessFactory::getForkedProcess(const msg::ForkChild &fc,
                                                Process * const parent) {
  auto f = new ForkedProcess(fc.pid(), fc.ppid(), parent);
  return f;
}

ForkedProcess* ProcessFactory::getForkedProcess(const int pid,
                                                Process * const parent) {
  return new ForkedProcess(pid, parent->pid(), parent);
}

ExecedProcess*
ProcessFactory::getExecedProcess(const msg::ShortCutProcessQuery &scpq, Process * parent,
                                 ExecedProcessCacher *cacher) {
  auto e = new ExecedProcess(scpq.pid(), scpq.ppid(), scpq.cwd(),
                             scpq.executable(), parent, cacher);

  for (int i = 0; i < scpq.arg_size(); i++) {
    e->args().push_back(scpq.arg(i));
  }
  for (int i = 0; i < scpq.env_var_size(); i++) {
    e->env_vars().push_back(scpq.env_var(i));
  }
  // TODO(rbalint) keep files in a separate container and refer to them instead
  // of creating the same strings several times
  for (int i = 0; i < scpq.libs().file_size(); i++) {
    e->libs().push_back(scpq.libs().file(i));
  }
  return e;
}

}  // namespace firebuild
