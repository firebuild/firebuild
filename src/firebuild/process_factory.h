/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_PROCESS_FACTORY_H_
#define FIREBUILD_PROCESS_FACTORY_H_

#include <memory>
#include <vector>

#include "./fbb.h"
#include "firebuild/execed_process.h"
#include "firebuild/forked_process.h"
#include "firebuild/process_tree.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild {

/**
 * Converts ProtoBuf messages from monitored processes to new Process
 * instances. It is an implementation of the GoF Factory pattern.
 * The class itself is never instantiated, but groups a set of
 * static functions which accept a ProcessTree reference and an incoming ProtoBuf
 * message to the process from.
 */
class ProcessFactory {
 public:
  static ForkedProcess* getForkedProcess(int pid, Process * const parent);
  static ExecedProcess* getExecedProcess(const FBB_scproc_query *msg,
                                         Process * parent,
                                         std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds);

 private:
  DISALLOW_COPY_AND_ASSIGN(ProcessFactory);
};

}  // namespace firebuild
#endif  // FIREBUILD_PROCESS_FACTORY_H_
