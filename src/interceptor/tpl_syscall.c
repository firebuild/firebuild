{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the syscall() call.                                   #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block no_intercept
  /* futex() doesn't have a glibc wrapper, pthread_mutex_[un]lock()
   * maps into syscall(SYS_futex, ...).
   * Don't need to notify the supervisor about these, stay out of the
   * way as much as possible. */
  if (number == SYS_futex
#ifdef SYS_futex_time64
                           || number == SYS_futex_time64
#endif
                                                        ) {
    i_am_intercepting = false;
  }
### endblock no_intercept
