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
    FBB_Builder_system ic_msg;
    fbb_system_init(&ic_msg);
    fbb_system_set_cmd(&ic_msg, cmd);
    fb_fbb_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
  }
### endblock before

### block send_msg
  {
    /* Notify the supervisor after the call */
    FBB_Builder_system_ret ic_msg;
    fbb_system_ret_init(&ic_msg);
    fbb_system_ret_set_cmd(&ic_msg, cmd);
    fbb_system_ret_set_ret(&ic_msg, ret);
    fbb_system_ret_set_error_no(&ic_msg, saved_errno);
    fb_fbb_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
    pthread_mutex_unlock(&ic_system_popen_lock);
  }
### endblock send_msg
