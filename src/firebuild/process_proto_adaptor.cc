/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/process_proto_adaptor.h"

#include <string>

#include "firebuild/execed_process.h"

namespace firebuild {
int ProcessPBAdaptor::msg(Process *p, const FBBCOMM_Serialized_open *o) {
  const int dirfd = fbbcomm_serialized_open_get_dirfd_with_fallback(o, AT_FDCWD);
  int error = fbbcomm_serialized_open_get_error_no_with_fallback(o, 0);
  int ret = fbbcomm_serialized_open_get_ret_with_fallback(o, -1);
  return p->handle_open(dirfd, fbbcomm_serialized_open_get_file(o),
                        fbbcomm_serialized_open_get_flags(o), ret, error, true);
}

int ProcessPBAdaptor::msg(Process *p, const FBBCOMM_Serialized_dlopen *dlo) {
  if (!fbbcomm_serialized_dlopen_has_error_no(dlo) &&
      fbbcomm_serialized_dlopen_has_absolute_filename(dlo)) {
    return p->handle_open(AT_FDCWD, fbbcomm_serialized_dlopen_get_absolute_filename(dlo),
                          O_RDONLY, -1, 0, false);
  } else {
    std::string filename = fbbcomm_serialized_dlopen_has_absolute_filename(dlo) ?
                           fbbcomm_serialized_dlopen_get_absolute_filename(dlo) : "NULL";
    p->exec_point()->disable_shortcutting_bubble_up("Process failed to dlopen() ", filename);
    return 0;
  }
}

int ProcessPBAdaptor::msg(Process *p, const FBBCOMM_Serialized_close *c) {
  const int error = fbbcomm_serialized_close_get_error_no_with_fallback(c, 0);
  return p->handle_close(fbbcomm_serialized_close_get_fd(c), error);
}

int ProcessPBAdaptor::msg(Process *p, const FBBCOMM_Serialized_unlink *u) {
  const int dirfd = fbbcomm_serialized_unlink_get_dirfd_with_fallback(u, AT_FDCWD);
  const int flags = fbbcomm_serialized_unlink_get_flags_with_fallback(u, 0);
  const int error = fbbcomm_serialized_unlink_get_error_no_with_fallback(u, 0);
  return p->handle_unlink(dirfd, fbbcomm_serialized_unlink_get_pathname(u), flags, error);
}

int ProcessPBAdaptor::msg(Process *p, const FBBCOMM_Serialized_rmdir *r) {
  const int error = fbbcomm_serialized_rmdir_get_error_no_with_fallback(r, 0);
  return p->handle_rmdir(fbbcomm_serialized_rmdir_get_pathname(r), error);
}

int ProcessPBAdaptor::msg(Process *p, const FBBCOMM_Serialized_mkdir *m) {
  const int dirfd = fbbcomm_serialized_mkdir_get_dirfd_with_fallback(m, AT_FDCWD);
  const int error = fbbcomm_serialized_mkdir_get_error_no_with_fallback(m, 0);
  return p->handle_mkdir(dirfd, fbbcomm_serialized_mkdir_get_pathname(m), error);
}

int ProcessPBAdaptor::msg(Process *p, const FBBCOMM_Serialized_dup3 *d) {
  const int error = fbbcomm_serialized_dup3_get_error_no_with_fallback(d, 0);
  const int flags = fbbcomm_serialized_dup3_get_flags_with_fallback(d, 0);
  return p->handle_dup3(fbbcomm_serialized_dup3_get_oldfd(d), fbbcomm_serialized_dup3_get_newfd(d),
                        flags, error);
}

int ProcessPBAdaptor::msg(Process *p, const FBBCOMM_Serialized_dup *d) {
  const int error = fbbcomm_serialized_dup_get_error_no_with_fallback(d, 0);
  return p->handle_dup3(fbbcomm_serialized_dup_get_oldfd(d), fbbcomm_serialized_dup_get_ret(d),
                        0, error);
}

int ProcessPBAdaptor::msg(Process *p, const FBBCOMM_Serialized_rename *r) {
  const int olddirfd = fbbcomm_serialized_rename_get_olddirfd_with_fallback(r, AT_FDCWD);
  const int newdirfd = fbbcomm_serialized_rename_get_newdirfd_with_fallback(r, AT_FDCWD);
  const int error = fbbcomm_serialized_rename_get_error_no_with_fallback(r, 0);
  return p->handle_rename(olddirfd, fbbcomm_serialized_rename_get_oldpath(r),
                          newdirfd, fbbcomm_serialized_rename_get_newpath(r), error);
}

int ProcessPBAdaptor::msg(Process *p, const FBBCOMM_Serialized_symlink *s) {
  const int newdirfd = fbbcomm_serialized_symlink_get_newdirfd_with_fallback(s, AT_FDCWD);
  const int error = fbbcomm_serialized_symlink_get_error_no_with_fallback(s, 0);
  return p->handle_symlink(fbbcomm_serialized_symlink_get_oldpath(s), newdirfd,
                           fbbcomm_serialized_symlink_get_newpath(s), error);
}

int ProcessPBAdaptor::msg(Process *p, const FBBCOMM_Serialized_fcntl *f) {
  const int error = fbbcomm_serialized_fcntl_get_error_no_with_fallback(f, 0);
  int arg = fbbcomm_serialized_fcntl_get_arg_with_fallback(f, 0);
  int ret = fbbcomm_serialized_fcntl_get_ret_with_fallback(f, -1);
  return p->handle_fcntl(fbbcomm_serialized_fcntl_get_fd(f), fbbcomm_serialized_fcntl_get_cmd(f),
                         arg, ret, error);
}

int ProcessPBAdaptor::msg(Process *p, const FBBCOMM_Serialized_ioctl *f) {
  const int error = fbbcomm_serialized_ioctl_get_error_no_with_fallback(f, 0);
  int ret = fbbcomm_serialized_ioctl_get_ret_with_fallback(f, -1);
  return p->handle_ioctl(fbbcomm_serialized_ioctl_get_fd(f), fbbcomm_serialized_ioctl_get_cmd(f),
                         ret, error);
}

int ProcessPBAdaptor::msg(Process *p, const FBBCOMM_Serialized_read_from_inherited *r) {
  const int error = fbbcomm_serialized_read_from_inherited_get_error_no_with_fallback(r, 0);
  if (error == 0) {
    p->handle_read_from_inherited(fbbcomm_serialized_read_from_inherited_get_fd(r));
  }
  return 0;
}

int ProcessPBAdaptor::msg(Process *p, const FBBCOMM_Serialized_write_to_inherited *w) {
  const int error = fbbcomm_serialized_write_to_inherited_get_error_no_with_fallback(w, 0);
  if (error == 0) {
    p->handle_write_to_inherited(fbbcomm_serialized_write_to_inherited_get_fd(w));
  }
  return 0;
}

int ProcessPBAdaptor::msg(Process *p, const FBBCOMM_Serialized_chdir *c) {
  const int error = fbbcomm_serialized_chdir_get_error_no_with_fallback(c, 0);
  if (error == 0) {
    p->handle_set_wd(fbbcomm_serialized_chdir_get_dir(c));
  } else {
    p->handle_fail_wd(fbbcomm_serialized_chdir_get_dir(c));
  }
  return 0;
}

int ProcessPBAdaptor::msg(Process *p, const FBBCOMM_Serialized_fchdir *f) {
  const int error = fbbcomm_serialized_fchdir_get_error_no_with_fallback(f, 0);
  if (error == 0) {
    p->handle_set_fwd(fbbcomm_serialized_fchdir_get_fd(f));
  }
  return 0;
}

}  // namespace firebuild
