{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for clone().                                              #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

{% set msg_skip_fields = ["fn", "stack", "flags", "arg"] %}

### block before
  /* clone() can be really tricky to intercept, for example when the cloned process shares
   * the file descriptor table with the parent (CLONE_FILES). In this case the interceptor
   * would have to protect two communication fds or implement locking across separate processes. */
  intercepting_enabled = false;
### endblock before

### block call_orig
  int vararg_count = 0;
  if (flags & (CLONE_CHILD_CLEARTID | CLONE_CHILD_SETTID)) {
    vararg_count = 3;
  } else if (flags & CLONE_SETTLS) {
    vararg_count = 2;
  } else if (flags & (CLONE_PARENT_SETTID | CLONE_PIDFD)) {
    vararg_count = 1;
  }
  if (vararg_count == 0) {
    ret = IC_ORIG({{ func }})(fn, stack, flags, arg);
  } else {
    pid_t *parent_tid = va_arg(ap, pid_t *);
    if (vararg_count == 1) {
      ret = IC_ORIG({{ func }})(fn, stack, flags, arg, parent_tid);
    } else {
      void *tls = va_arg(ap, void *);
      if (vararg_count == 2) {
        ret = IC_ORIG({{ func }})(fn, stack, flags, arg, parent_tid, tls);
      } else {
        pid_t *child_tid = va_arg(ap, pid_t *);
        ret = IC_ORIG({{ func }})(fn, stack, flags, arg, parent_tid, tls, child_tid);
      }
    }
  }
### endblock call_orig
