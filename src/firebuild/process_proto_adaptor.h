/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_PROCESS_PROTO_ADAPTOR_H_
#define FIREBUILD_PROCESS_PROTO_ADAPTOR_H_

#include "./fb-messages.pb.h"
#include "firebuild/process.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild  {
  /**
   * Converts ProtoBuf messages from monitored processes to calls to Process
   * instances.
   * It is not a clean implementation of the GoF Adaptor pattern, but something
   * like that. The class itself is never instantiated, but groups a set of
   * static functions which accept a Process reference and an incoming ProtoBuf
   * message for the process.
   */
class ProcessPBAdaptor {
 public:
  static int msg(Process *p, const msg::Open &o);
  static int msg(Process *p, const msg::Close &c);
  static int msg(Process *p, const msg::Pipe2 &pipe);
  static int msg(Process *p, const msg::Dup &d);
  static int msg(Process *p, const msg::Dup3 &d);
  static int msg(Process *p, const msg::Fcntl &f);
  static int msg(Process *p, const msg::ChDir &c);

 private:
  DISALLOW_COPY_AND_ASSIGN(ProcessPBAdaptor);
};

}  // namespace firebuild
#endif  // FIREBUILD_PROCESS_PROTO_ADAPTOR_H_
