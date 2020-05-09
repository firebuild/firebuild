{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the fork() call.                                      #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block send_msg
  if (!success) {
    /* Error */

    // FIXME: disable shortcutting

  } else if (ret == 0) {
    /* Child */

    reset_interceptors();
    ic_pid = getpid();
    // unlock global interceptor lock if it is locked
    pthread_mutex_trylock(&ic_global_lock);
    pthread_mutex_unlock(&ic_global_lock);
    // reconnect to supervisor
    fb_init_supervisor_conn();

    /* Notify the supervisor */
    msg::InterceptorMsg ic_msg;
    auto m = ic_msg.mutable_fork_child();
    m->set_pid(ic_pid);
    m->set_ppid(getppid());
    fb_send_msg_and_check_ack(ic_msg, fb_sv_conn);
  } else {
    /* Parent */

    /* Notify the supervisor */
    msg::InterceptorMsg ic_msg;
    auto m = ic_msg.mutable_fork_parent();
    m->set_pid(ret);
    fb_send_msg(ic_msg, fb_sv_conn);
  }
### endblock send_msg
