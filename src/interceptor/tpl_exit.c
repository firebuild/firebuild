{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the exit() call (which calls the atexit / on_exit     #}
{# handlers).                                                         #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block body
  /* Exit handlers may call intercepted functions, so release the lock */
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

  /* Perform the call.
   * This will call the registered atexit / on_exit handlers,
   * including our handle_exit() which will notify the supervisor. */
  IC_ORIG({{ func }})({{ names_str }});

  /* Make scan-build happy */
  (void)i_locked;

  /* Should not be reached */
  assert(0 && "{{ func }} did not exit");
  abort(); /* for NDEBUG */
### endblock body
