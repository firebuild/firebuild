#ifndef FIREBUILD_PROCESSPBADAPTOR_H
#define FIREBUILD_PROCESSPBADAPTOR_H

#include "fb-messages.pb.h"
#include "Process.h"
#include "cxx_lang_utils.h"

namespace firebuild 
{
  /**
   * Converts ProtoBuf messages from monitored processes to calls to Process
   * instances.
   * It is not a clean implementation of the GoF Adaptor pattern, but something
   * like that. The class itself is never instantiated, but groups a set of
   * static functions which accept a Process reference and an incoming ProtoBuf
   * message for the process.
   */
  class ProcessPBAdaptor
{
 public:
  static int msg(Process &p, const msg::Open &o);
  static int msg(Process &p, const msg::Close &c);
  static int msg(Process &p, const msg::ChDir &c);
 private:
  DISALLOW_COPY_AND_ASSIGN(ProcessPBAdaptor);
};

}
#endif
