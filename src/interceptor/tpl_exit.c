{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the exit() call (which calls the atexit / on_exit     #}
{# handlers).                                                         #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block body
  /* Exit handlers may call intercepted functions */
  intercept_on = NULL;

  /* Mark the end now */
  insert_end_marker("{{ func }}");

  /* Perform the call.
   * This will call the registered atexit / on_exit handlers,
   * including our handle_exit() which will notify the supervisor. */
  ic_orig_{{ func }}({{ names_str }});

  /* Should not be reached */
  assert(0 && "{{ func }} did not exit");
### endblock body
