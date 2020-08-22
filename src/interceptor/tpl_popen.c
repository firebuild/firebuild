{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the popen() call.                                     #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block before
  {
    pthread_mutex_lock(&ic_system_popen_lock);
    /* Notify the supervisor before the call */
    FBB_Builder_popen ic_msg;
    fbb_popen_init(&ic_msg);
    fbb_popen_set_cmd(&ic_msg, cmd);
    fbb_popen_set_type(&ic_msg, type);
    fb_fbb_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
  }
### endblock before

### block send_msg
  {
    /* Notify the supervisor after the call */
    if (success) {
      FBB_Builder_popen_parent ic_msg;
      fbb_popen_parent_init(&ic_msg);
      fbb_popen_parent_set_fd(&ic_msg, ic_orig_fileno(ret));
      fb_fbb_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
    } else {
      FBB_Builder_popen_failed ic_msg;
      fbb_popen_failed_init(&ic_msg);
      fbb_popen_failed_set_error_no(&ic_msg, saved_errno);
      fb_fbb_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
    }
    pthread_mutex_unlock(&ic_system_popen_lock);
  }
### endblock send_msg
