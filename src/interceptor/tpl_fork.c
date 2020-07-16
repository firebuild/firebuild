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
  pthread_sigmask(SIG_SETMASK, &set_block_all, &set_orig);
### endblock before

### block after
  if (!success) {
    /* Error */

    // FIXME: disable shortcutting

  } else if (ret == 0) {
    /* Child */

    /* Reinitialize the lock, see #207 */
    ic_global_lock = PTHREAD_MUTEX_INITIALIZER;
    /* Relocking is pretty pointless since a forked child is always
     * single-threaded. Anyway, let's maintain internal consistency and
     * let's not make the closing unlock() fail. */
    if (i_am_intercepting) {
      pthread_mutex_lock(&ic_global_lock);
      assert(thread_has_global_lock);
    }

    /* Reinitialize other stuff */
    reset_interceptors();
    ic_pid = getpid();

    /* Reconnect to supervisor */
    fb_init_supervisor_conn();
  } else {
    /* Parent, nothing here to do */
  }
### endblock after

### block send_msg
  /* Notify the supervisor */
  if (!success) {
    /* Error, nothing here to do */
  } else if (ret == 0) {
    /* Child */
    msg::InterceptorMsg ic_msg;
    auto m = ic_msg.mutable_fork_child();
    m->set_pid(ic_pid);
    m->set_ppid(getppid());
    fb_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
  } else {
    /* Parent */
    msg::InterceptorMsg ic_msg;
    auto m = ic_msg.mutable_fork_parent();
    m->set_pid(ret);
    fb_send_msg(&ic_msg, fb_sv_conn);
  }

  /* Common for all three outcomes: re-enable signal delivery */
  pthread_sigmask(SIG_SETMASK, &set_orig, NULL);
### endblock send_msg
