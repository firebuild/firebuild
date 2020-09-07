{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the fork() call.                                      #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block before
  /* Make sure the child cannot receive a signal until it builds up
   * the new connection to the supervisor. To do this, we must block
   * signals before forking. */
  sigset_t set_orig, set_block_all;
  sigfillset(&set_block_all);
  (*ic_pthread_sigmask)(SIG_SETMASK, &set_block_all, &set_orig);

  thread_libc_nesting_depth++;
### endblock before

### block after
  thread_libc_nesting_depth--;

  if (!success) {
    /* Error */
    // FIXME: disable shortcutting
  }
  /* In the child, what we need to do here is done via our atfork_child_handler().
   * In the parent there's nothing to do here at all. */
### endblock after

### block send_msg
  /* Notify the supervisor */
  if (!success) {
    /* Error, nothing here to do */
  } else if (ret == 0) {
    /* The child signed in to the supervisor in atfork_child_handler(), nothing else here to do. */
  } else {
    /* Parent */
    FBB_Builder_fork_parent ic_msg;
    fbb_fork_parent_init(&ic_msg);
    fbb_fork_parent_set_pid(&ic_msg, ret);
    fb_fbb_send_msg(&ic_msg, fb_sv_conn);
  }

  /* Common for all three outcomes: re-enable signal delivery */
  (*ic_pthread_sigmask)(SIG_SETMASK, &set_orig, NULL);
### endblock send_msg
