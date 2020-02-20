{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the popen() call.                                     #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block before
  {
    /* Notify the supervisor before the call */
    msg::InterceptorMsg ic_msg;
    auto m = ic_msg.mutable_popen();
    if (cmd != NULL) m->set_cmd(cmd);
    if (type != NULL) m->set_type(type);
    fb_send_msg_and_check_ack(ic_msg, fb_sv_conn);
  }
### endblock before

### block send_msg
  {
    /* Notify the supervisor after the call */
    msg::InterceptorMsg ic_msg;
    if (success) {
      auto m = ic_msg.mutable_popen_parent();
      m->set_fd(ic_orig_fileno(ret));
    } else {
      auto m = ic_msg.mutable_popen_failed();
      m->set_error_no(saved_errno);
    }
    fb_send_msg_and_check_ack(ic_msg, fb_sv_conn);
  }
### endblock send_msg
