{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the _exit() family (which exit immediately, skipping  #}
{# the atexit / on_exit handlers).                                    #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block body
  /* Mark the end now */
  insert_end_marker("{{ func }}");

  /* Notify the supervisor by calling handle_exit() */
  handle_exit({{ names_str }});

  /* Perform the call */
  ic_orig_{{ func }}({{ names_str }});

  /* Should not be reached */
  assert(0 && "{{ func }} did not exit");
### endblock body
