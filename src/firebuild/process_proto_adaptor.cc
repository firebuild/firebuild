/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/process_proto_adaptor.h"

#include <string>

namespace firebuild {
int ProcessPBAdaptor::msg(Process *p, const msg::Open &o) {
  int error = (o.has_error_no())?o.error_no():0;
  int ret = (o.has_ret())?o.ret():-1;
  return p->handle_open(o.file(), o.flags(), ret, error);
}

int ProcessPBAdaptor::msg(Process *p, const msg::DLOpen &dlo) {
  if (!dlo.has_error_no() && dlo.has_absolute_filename()) {
    return p->handle_open(dlo.absolute_filename(), O_RDONLY, -1, 0);
  } else {
    std::string filename = dlo.has_filename() ? dlo.filename() : "NULL";
    p->disable_shortcutting("Process failed to dlopen() " + filename);
    return 0;
  }
}

int ProcessPBAdaptor::msg(Process *p, const msg::Close &c) {
  const int error = (c.has_error_no())?c.error_no():0;
  return p->handle_close(c.fd(), error);
}

int ProcessPBAdaptor::msg(Process *p, const msg::Pipe2 &pipe) {
  const int fd0 = (pipe.has_fd0())?pipe.fd0():-1;
  const int fd1 = (pipe.has_fd1())?pipe.fd1():-1;
  const int flags = (pipe.has_flags())?pipe.flags():0;
  const int error = (pipe.has_error_no())?pipe.error_no():0;
  return p->handle_pipe(fd0, fd1, flags, error);
}

int ProcessPBAdaptor::msg(Process *p, const msg::Dup3 &d) {
  const int error = (d.has_error_no())?d.error_no():0;
  const int flags = (d.has_flags())?d.flags():0;
  return p->handle_dup3(d.oldfd(), d.newfd(), flags, error);
}

int ProcessPBAdaptor::msg(Process *p, const msg::Dup &d) {
  const int error = (d.has_error_no())?d.error_no():0;
  return p->handle_dup3(d.oldfd(), d.ret(), 0, error);
}

int ProcessPBAdaptor::msg(Process *p, const msg::Fcntl &f) {
  const int error = (f.has_error_no())?f.error_no():0;
  int arg = (f.has_arg())?f.arg():0;
  int ret = (f.has_ret())?f.ret():-1;
  return p->handle_fcntl(f.fd(), f.cmd(), arg, ret, error);
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
