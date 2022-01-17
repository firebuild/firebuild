{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for clone().                                              #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

{% set msg_skip_fields = ["fn", "stack", "flags", "arg"] %}

### block before
  /* seemingly unused, actually used via __builtin_apply_args */
  (void) fn;
  (void) stack;
  /* clone() can be really tricky to intercept, for example when the cloned process shares
   * the file descriptor table with the parent (CLONE_FILES). In this case the interceptor
   * would have to protect two communication fds or implement locking across separate processes. */
  intercepting_enabled = false;
### endblock before
