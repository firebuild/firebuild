{# ------------------------------------------------------------------ #}
{# Copyright (c) 2022 Firebuild Inc.                                  #}
{# All rights reserved.                                               #}
{# Free for personal use and commercial trial.                        #}
{# Non-trial commercial use requires licenses available from          #}
{# https://firebuild.com                                              #}
{# ------------------------------------------------------------------ #}
{# Template for the system() call.                                    #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block before
  {
    pthread_mutex_lock(&ic_system_popen_lock);
    /* Notify the supervisor before the call */
    FBBCOMM_Builder_system ic_msg;
    fbbcomm_builder_system_init(&ic_msg);
    fbbcomm_builder_system_set_cmd(&ic_msg, cmd);
    fb_fbbcomm_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
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
    FBBCOMM_Builder_system_ret ic_msg;
    fbbcomm_builder_system_ret_init(&ic_msg);
    fbbcomm_builder_system_ret_set_cmd(&ic_msg, cmd);
    fbbcomm_builder_system_ret_set_ret(&ic_msg, ret);
    fbbcomm_builder_system_ret_set_error_no(&ic_msg, saved_errno);
    fb_fbbcomm_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
    pthread_mutex_unlock(&ic_system_popen_lock);
  }
### endblock send_msg
