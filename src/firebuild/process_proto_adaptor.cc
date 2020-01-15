/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/process_proto_adaptor.h"

namespace firebuild {
int ProcessPBAdaptor::msg(Process *p, const msg::Open &o) {
  bool c = (o.has_created())?o.created():false;
  int error = (o.has_error_no())?o.error_no():0;
  return p->open_file(o.file(), o.flags(), o.mode(), o.ret(), c, error);
}

int ProcessPBAdaptor::msg(Process *p, const msg::Close &c) {
  const int error = (c.has_error_no())?c.error_no():0;
  return p->close_file(c.fd(), error);
}

int ProcessPBAdaptor::msg(Process *p, const msg::Pipe2 &pipe) {
  const int error = (pipe.has_error_no())?pipe.error_no():0;
  const int flags = (pipe.has_flags())?pipe.flags():0;
  return p->create_pipe(pipe.fd0(), pipe.fd1(), flags, error);
}

int ProcessPBAdaptor::msg(Process *p, const msg::Dup3 &d) {
  const int error = (d.has_error_no())?d.error_no():0;
  const int flags = (d.has_flags())?d.flags():0;
  return p->dup3(d.oldfd(), d.newfd(), flags, error);
}

int ProcessPBAdaptor::msg(Process *p, const msg::Dup &d) {
  const int error = (d.has_error_no())?d.error_no():0;
  return p->dup3(d.oldfd(), d.ret(), 0, error);
}

int ProcessPBAdaptor::msg(Process *p, const msg::ChDir &c) {
  const int error = (c.has_error_no())?c.error_no():0;
  if (0 == error) {
    p->set_wd(c.dir());
  } else {
    p->fail_wd(c.dir());
  }
  return 0;
}

}  // namespace firebuild
