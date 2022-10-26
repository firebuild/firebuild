{# ------------------------------------------------------------------ #}
{# Copyright (c) 2022 Firebuild Inc.                                  #}
{# All rights reserved.                                               #}
{# Free for personal use and commercial trial.                        #}
{# Non-trial commercial use requires licenses available from          #}
{# https://firebuild.com                                              #}
{# ------------------------------------------------------------------ #}
{# Template for the _exit() family (which exit immediately, skipping  #}
{# the atexit / on_exit handlers).                                    #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block body
  /* Release the lock, to resemble tpl_exit.c.
   * handle_exit() will re-grab it. */
  thread_signal_danger_zone_enter();
  if (thread_has_global_lock) {
    pthread_mutex_unlock(&ic_global_lock);
    thread_has_global_lock = false;
    thread_intercept_on = NULL;
  }
  thread_signal_danger_zone_leave();
  assert(thread_signal_danger_zone_depth == 0);

  /* Mark the end now */
  insert_end_marker("{{ func }}");

  /* Notify the supervisor by calling handle_exit() */
  handle_exit({{ names_str }});

  /* Perform the call */
  ic_orig_{{ func }}({{ names_str }});

  /* Make scan-build happy */
  (void)i_locked;

  /* Should not be reached */
  assert(0 && "{{ func }} did not exit");
  abort(); /* for NDEBUG */
### endblock body
