{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the fcntl() family.                                   #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block no_intercept
  /* futex() doesn't have a glibc wrapper, pthread_mutex_[un]lock()
   * maps into syscall(__NR_futex, ...).
   * We don't need to notify the supervisor about these.
   * More importantly, we must return before locking, in order to
   * avoid a deadlock with this very same mutex that we guard the
   * communication with. */
  if (number == __NR_futex) {
    void *args = __builtin_apply_args();
    {%+ if rettype != 'void' %}void const * const result ={% endif -%}
    __builtin_apply((void (*)(...))(void *)ic_orig_{{ func }}, args, 100);
    {%+ if rettype != 'void' %}ret = *({{ rettype }}*)result;{% endif %}

    return ret;
  }
### endblock no_intercept
