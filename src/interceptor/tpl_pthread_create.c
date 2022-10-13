{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for pthread_create, inherited from marker_only.           #}
{# Insert another trace markers, telling the pid.                     #}
{# ------------------------------------------------------------------ #}
### extends "tpl_marker_only.c"

{% set msg = None %}
{% set global_lock = False %}

### block no_intercept
  i_am_intercepting = false;
  (void)i_am_intercepting;
### endblock no_intercept

### block call_orig
  /* Need to pass two pointers using one. Allocate room on the heap,
   * placing it on the stack might not live long enough.
   * Will be free()d in pthread_start_routine_wrapper(). */
  void **routine_and_arg = malloc(2 * sizeof(void *));
  routine_and_arg[0] = start_routine;
  routine_and_arg[1] = arg;
  ret = ic_orig_pthread_create(thread, attr, pthread_start_routine_wrapper, routine_and_arg);
### endblock call_orig
