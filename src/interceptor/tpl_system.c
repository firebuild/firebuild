{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the system() call.                                    #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block before
  {
    pthread_mutex_lock(&ic_system_popen_lock);
    /* Notify the supervisor before the call */
    msg::InterceptorMsg ic_msg;
    auto m = ic_msg.mutable_system();
    if (cmd != NULL) m->set_cmd(cmd);
    fb_send_msg_and_check_ack(ic_msg, fb_sv_conn);
  }
### endblock before

### block send_msg
  {
    /* Notify the supervisor after the call */
    msg::InterceptorMsg ic_msg;
    auto m = ic_msg.mutable_system_ret();
    if (cmd != NULL) m->set_cmd(cmd);
    m->set_ret(ret);
    m->set_error_no(saved_errno);
    fb_send_msg_and_check_ack(ic_msg, fb_sv_conn);
    pthread_mutex_unlock(&ic_system_popen_lock);
  }
### endblock send_msg
