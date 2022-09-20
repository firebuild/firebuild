{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for clone().                                              #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

{% set msg_skip_fields = ["fn", "stack", "arg"] %}

### block call_orig
  if (i_am_intercepting) {
    pre_clone_disable_interception(flags, false, &i_locked);
  }

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

### block send_msg
### endblock send_msg
