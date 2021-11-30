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
