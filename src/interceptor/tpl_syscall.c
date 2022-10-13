{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the syscall() call.                                   #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block no_intercept
  /* Interesting syscalls are redirected to go via the glibc wrapper, others are just let
     through. */
  switch (number) {
    /* futex() doesn't have a glibc wrapper, pthread_mutex_[un]lock()
     * maps into syscall(SYS_futex, ...).
     * Don't need to notify the supervisor about these, stay out of the
     * way as much as possible. */
    case SYS_futex:
#ifdef SYS_futex_time64
    case SYS_futex_time64:
#endif
      {
        i_am_intercepting = false;
        break;
      }
    case SYS_close:
      {
        int fd = va_arg(ap, int);
        va_end(ap);
        return close(fd);
        break;
      }
    case SYS_pipe2:
      {
        int *pipe_fds = va_arg(ap, int*);
        int flags = va_arg(ap, int);
        va_end(ap);
        return pipe2(pipe_fds, flags);
        break;
      }
    case SYS_chdir:
      {
        char* path = va_arg(ap, char*);
        va_end(ap);
        return chdir(path);
        break;
      }
    case SYS_fchdir:
      {
        int fd = va_arg(ap, int);
        va_end(ap);
        return fchdir(fd);
        break;
      }
  }
### endblock no_intercept

### block call_orig
  switch (number) {
    case SYS_clone:
      {
        /* Need to extract 'flags'. See clone(2) NOTES about differences between architectures. */
#if defined(__s390__) || defined(__cris__)
        va_arg(ap, void*);  /* skip over 'stack' */
#endif
        unsigned long flags = va_arg(ap, unsigned long);
        va_end(ap);
        if (i_am_intercepting) {
          pre_clone_disable_interception(flags, true, &i_locked);
          i_am_intercepting = false;
        }
        break;
      }
  }
  /* Pass on several long parameters unchanged, see #178. */
  va_list ap_pass;
  va_start(ap_pass, number);
  long arg1 = va_arg(ap_pass, long);
  long arg2 = va_arg(ap_pass, long);
  long arg3 = va_arg(ap_pass, long);
  long arg4 = va_arg(ap_pass, long);
  long arg5 = va_arg(ap_pass, long);
  long arg6 = va_arg(ap_pass, long);
  long arg7 = va_arg(ap_pass, long);
  long arg8 = va_arg(ap_pass, long);
  va_end(ap_pass);
  ret = ic_orig_{{ func }}(number, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
### endblock call_orig
