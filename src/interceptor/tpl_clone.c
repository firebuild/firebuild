{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for clone().                                              #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

{% set msg_skip_fields = ["fn", "stack", "arg"] %}

### block call_orig
  int vararg_count = 0;
  if (flags & (CLONE_CHILD_CLEARTID | CLONE_CHILD_SETTID)) {
    vararg_count = 3;
  } else if (flags & CLONE_SETTLS) {
    vararg_count = 2;
  } else if (flags & (CLONE_PARENT_SETTID | CLONE_PIDFD)) {
    vararg_count = 1;
  }

  if (i_am_intercepting) {
    FBBCOMM_Builder_clone ic_msg;
    fbbcomm_builder_clone_init(&ic_msg);

    /* Skipping 'fn' */
    /* Skipping 'stack' */
    fbbcomm_builder_clone_set_flags(&ic_msg, flags);
    /* Skipping 'arg' */
    /* Not sending return value */
    /* Send and go on, no ack */
    fb_fbbcomm_send_msg(&ic_msg, fb_sv_conn);

    /* clone() can be really tricky to intercept, for example when the cloned process shares
     * the file descriptor table with the parent (CLONE_FILES). In this case the interceptor
     * would have to protect two communication fds or implement locking across separate processes. */
    intercepting_enabled = false;
    env_purge(environ);
    /* Releasing the global lock (if we grabbed it in this pass) to not keep it locked in the forked process. */
    if (i_locked) {
      release_global_lock();
      i_locked = false;
    }
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
