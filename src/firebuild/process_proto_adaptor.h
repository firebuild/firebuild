/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_PROCESS_PROTO_ADAPTOR_H_
#define FIREBUILD_PROCESS_PROTO_ADAPTOR_H_

#include "./fbbcomm.h"
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
  static int msg(Process *p, const FBBCOMM_Serialized_open *o, int fd_conn, int ack_num);
  static int msg(Process *p, const FBBCOMM_Serialized_freopen *fro, int fd_conn, int ack_num);
  static int msg(Process *p, const FBBCOMM_Serialized_dlopen *dlo, int fd_conn, int ack_num);
  static int msg(Process *p, const FBBCOMM_Serialized_close *c);
  static int msg(Process *p, const FBBCOMM_Serialized_unlink *u);
  static int msg(Process *p, const FBBCOMM_Serialized_rmdir *r);
  static int msg(Process *p, const FBBCOMM_Serialized_mkdir *m);
  static int msg(Process *p, const FBBCOMM_Serialized_fstat *f);
  static int msg(Process *p, const FBBCOMM_Serialized_stat *s);
  static int msg(Process *p, const FBBCOMM_Serialized_dup *d);
  static int msg(Process *p, const FBBCOMM_Serialized_dup3 *d);
  static int msg(Process *p, const FBBCOMM_Serialized_rename *r);
  static int msg(Process *p, const FBBCOMM_Serialized_symlink *s);
  static int msg(Process *p, const FBBCOMM_Serialized_fcntl *f);
  static int msg(Process *p, const FBBCOMM_Serialized_ioctl *i);
  static int msg(Process *p, const FBBCOMM_Serialized_read_from_inherited *r);
  static int msg(Process *p, const FBBCOMM_Serialized_write_to_inherited *w);
  static int msg(Process *p, const FBBCOMM_Serialized_chdir *c);
  static int msg(Process *p, const FBBCOMM_Serialized_fchdir *f);

 private:
  DISALLOW_COPY_AND_ASSIGN(ProcessPBAdaptor);
};

}  /* namespace firebuild */
#endif  // FIREBUILD_PROCESS_PROTO_ADAPTOR_H_
