/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_PROCESSFACTORY_H_
#define FIREBUILD_PROCESSFACTORY_H_

#include "fb-messages.pb.h"
#include "ExecedProcess.h"
#include "ForkedProcess.h"
#include "ProcessTree.h"
#include "cxx_lang_utils.h"

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
  static ForkedProcess* getForkedProcess(const msg::ForkChild &fc,
                                         Process * const fork_parent);
  static ExecedProcess* getExecedProcess(const msg::ShortCutProcessQuery &scpq);

 private:
  DISALLOW_COPY_AND_ASSIGN(ProcessFactory);
};

}  // namespace firebuild
#endif  // FIREBUILD_PROCESSFACTORY_H_
