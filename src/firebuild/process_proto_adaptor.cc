/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/process_proto_adaptor.h"

#include <string>

namespace firebuild {
int ProcessPBAdaptor::msg(Process *p, const FBB_open *o) {
  int error = fbb_open_get_error_no_with_fallback(o, 0);
  int ret = fbb_open_get_ret_with_fallback(o, -1);
  return p->handle_open(fbb_open_get_file(o), fbb_open_get_flags(o), ret, error);
}

int ProcessPBAdaptor::msg(Process *p, const FBB_dlopen *dlo) {
  if (!fbb_dlopen_has_error_no(dlo) && fbb_dlopen_has_absolute_filename(dlo)) {
    return p->handle_open(fbb_dlopen_get_absolute_filename(dlo), O_RDONLY, -1, 0);
  } else {
    std::string filename = fbb_dlopen_has_absolute_filename(dlo) ?
                           fbb_dlopen_get_absolute_filename(dlo) : "NULL";
    p->disable_shortcutting_bubble_up("Process failed to dlopen() " + filename);
    return 0;
  }
}

int ProcessPBAdaptor::msg(Process *p, const FBB_close *c) {
  const int error = fbb_close_get_error_no_with_fallback(c, 0);
  return p->handle_close(fbb_close_get_fd(c), error);
}

int ProcessPBAdaptor::msg(Process *p, const FBB_pipe2 *pipe) {
  const int fd0 = fbb_pipe2_get_fd0_with_fallback(pipe, -1);
  const int fd1 = fbb_pipe2_get_fd1_with_fallback(pipe, -1);
  const int flags = fbb_pipe2_get_flags_with_fallback(pipe, 0);
  const int error = fbb_pipe2_get_error_no_with_fallback(pipe, 0);
  return p->handle_pipe(fd0, fd1, flags, error);
}

int ProcessPBAdaptor::msg(Process *p, const FBB_dup3 *d) {
  const int error = fbb_dup3_get_error_no_with_fallback(d, 0);
  const int flags = fbb_dup3_get_flags_with_fallback(d, 0);
  return p->handle_dup3(fbb_dup3_get_oldfd(d), fbb_dup3_get_newfd(d), flags, error);
}

int ProcessPBAdaptor::msg(Process *p, const FBB_dup *d) {
  const int error = fbb_dup_get_error_no_with_fallback(d, 0);
  return p->handle_dup3(fbb_dup_get_oldfd(d), fbb_dup_get_ret(d), 0, error);
}

int ProcessPBAdaptor::msg(Process *p, const FBB_fcntl *f) {
  const int error = fbb_fcntl_get_error_no_with_fallback(f, 0);
  int arg = fbb_fcntl_get_arg_with_fallback(f, 0);
  int ret = fbb_fcntl_get_ret_with_fallback(f, -1);
  return p->handle_fcntl(fbb_fcntl_get_fd(f), fbb_fcntl_get_cmd(f), arg, ret, error);
}

int ProcessPBAdaptor::msg(Process *p, const FBB_ioctl *f) {
  const int error = fbb_ioctl_get_error_no_with_fallback(f, 0);
  int ret = fbb_ioctl_get_ret_with_fallback(f, -1);
  return p->handle_ioctl(fbb_ioctl_get_fd(f), fbb_ioctl_get_cmd(f), ret, error);
}

int ProcessPBAdaptor::msg(Process *p, const FBB_write *w) {
  const int error = fbb_write_get_error_no_with_fallback(w, 0);
  if (error == 0) {
    p->handle_write(fbb_write_get_fd(w));
  }
  return 0;
}

int ProcessPBAdaptor::msg(Process *p, const FBB_chdir *c) {
  const int error = fbb_chdir_get_error_no_with_fallback(c, 0);
  if (error == 0) {
    p->set_wd(fbb_chdir_get_dir(c));
  } else {
    p->fail_wd(fbb_chdir_get_dir(c));
  }
  return 0;
}

}  // namespace firebuild
