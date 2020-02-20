{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the posix_spawn() family.                             #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block before
  {
    /* Notify the supervisor before the call */
    msg::InterceptorMsg ic_msg;
    auto m = ic_msg.mutable_posix_spawn();
    if (file != NULL) m->set_file(file);
###   if func == 'posix_spawnp'
    m->set_is_spawnp(true);
###   else
    m->set_is_spawnp(false);
###   endif
    for (int i = 0; argv[i] != NULL; i++) {
      m->add_arg(argv[i]);
    }
    for (int i = 0; envp[i] != NULL; i++) {
      m->add_env(envp[i]);
    }
    fb_send_msg_and_check_ack(ic_msg, fb_sv_conn);
  }
### endblock before

### block send_msg
  {
    /* Notify the supervisor after the call */
    msg::InterceptorMsg ic_msg;
    if (success) {
      auto m = ic_msg.mutable_posix_spawn_parent();
      m->set_pid(*pid);
    } else {
      auto m = ic_msg.mutable_posix_spawn_failed();
      m->set_error_no(saved_errno);
    }
    fb_send_msg_and_check_ack(ic_msg, fb_sv_conn);
  }
### endblock send_msg
