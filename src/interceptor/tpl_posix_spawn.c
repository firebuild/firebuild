{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the posix_spawn() family.                             #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block before
  {
    pthread_mutex_lock(&ic_system_popen_lock);
    /* Notify the supervisor before the call */
    FBB_Builder_posix_spawn ic_msg;
    fbb_posix_spawn_init(&ic_msg);
    fbb_posix_spawn_set_file(&ic_msg, file);
###   if func == 'posix_spawnp'
    fbb_posix_spawn_set_is_spawnp(&ic_msg, true);
###   else
    fbb_posix_spawn_set_is_spawnp(&ic_msg, false);
###   endif
    if (file_actions) {
      string_array *p = psfa_find(file_actions);
      assert(p);
      fbb_posix_spawn_set_file_actions(&ic_msg, p->p);
    }
    fbb_posix_spawn_set_arg(&ic_msg, argv);
    fbb_posix_spawn_set_env(&ic_msg, envp);
    fb_fbb_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
  }
### endblock before

### block call_orig
  /* Fix up the environment */
  void *env_fixed_up;
  if (env_needs_fixup((char **) envp)) {
    int env_fixup_size = get_env_fixup_size((char **) envp);
    env_fixed_up = alloca(env_fixup_size);
    env_fixup((char **) envp, env_fixed_up);
  } else {
    env_fixed_up = environ;
  }

  ret = ic_orig_{{ func }}({{ names_str | replace("envp", "env_fixed_up")}});
### endblock call_orig

### block send_msg
  {
    /* Notify the supervisor after the call */
    if (success) {
      FBB_Builder_posix_spawn_parent ic_msg;
      fbb_posix_spawn_parent_init(&ic_msg);
      fbb_posix_spawn_parent_set_pid(&ic_msg, *pid);
      fb_fbb_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
    } else {
      FBB_Builder_posix_spawn_failed ic_msg;
      fbb_posix_spawn_failed_init(&ic_msg);
      fbb_posix_spawn_failed_set_arg(&ic_msg, argv);
      fbb_posix_spawn_failed_set_error_no(&ic_msg, saved_errno);
      fb_fbb_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
    }
    pthread_mutex_unlock(&ic_system_popen_lock);
  }
### endblock send_msg
