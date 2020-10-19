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

### block call_orig
  /* Fix up the environment */
  /* This is racy because it operates on the global "environ", but is probably good enough. */
  /* A proper solution would require to prefix "cmd" with a wrapper that fixes it up, but that could be slow. */
  bool do_env_fixup = false;
  char **environ_saved = environ;
  if (env_needs_fixup(environ)) {
    do_env_fixup = true;
    int env_fixup_size = get_env_fixup_size(environ);
    environ = alloca(env_fixup_size);
    env_fixup(environ_saved, environ);
  }

  {{ super() }}

  if (do_env_fixup) {
    environ = environ_saved;
  }
### endblock call_orig

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
