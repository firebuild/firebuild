/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_PROCESS_PROTO_ADAPTOR_H_
#define FIREBUILD_PROCESS_PROTO_ADAPTOR_H_

#include "./fbb.h"
#include "firebuild/fd.h"
#include "firebuild/process.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild  {
  /**
   * Converts messages from monitored processes to calls to Process instances.
   * It is not a clean implementation of the GoF Adaptor pattern, but something
   * like that. The class itself is never instantiated, but groups a set of
   * static functions which accept a Process reference and an incoming ProtoBuf
   * message for the process.
   */
class ProcessPBAdaptor {
 public:
  static int msg(Process *p, const FBB_open *o, FD fd_conn, int ack_num);
  static int msg(Process *p, const FBB_dlopen *dlo, FD fd_conn, int ack_num);
  static int msg(Process *p, const FBB_close *c);
  static int msg(Process *p, const FBB_unlink *u);
  static int msg(Process *p, const FBB_rmdir *r);
  static int msg(Process *p, const FBB_mkdir *m);
  static int msg(Process *p, const FBB_dup *d);
  static int msg(Process *p, const FBB_dup3 *d);
  static int msg(Process *p, const FBB_rename *r);
  static int msg(Process *p, const FBB_symlink *s);
  static int msg(Process *p, const FBB_fcntl *f);
  static int msg(Process *p, const FBB_ioctl *i);
  static int msg(Process *p, const FBB_read *r);
  static int msg(Process *p, const FBB_write *w);
  static int msg(Process *p, const FBB_chdir *c);
  static int msg(Process *p, const FBB_fchdir *f);

 private:
  DISALLOW_COPY_AND_ASSIGN(ProcessPBAdaptor);
};

}  // namespace firebuild
#endif  // FIREBUILD_PROCESS_PROTO_ADAPTOR_H_
