{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for functions reading from a file.                        #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

{% set msg = "read" %}
{# No locking around the read(): see issue #279 #}
{% set global_lock = 'never' %}

### block send_msg
  {# Acquire the lock if sending a message #}
  if (fd < 0 || fd >= IC_FD_STATES_SIZE || ic_fd_states[fd].read == false) {
    /* Need to notify the supervisor */

    {{ grab_lock_if_needed('true') | indent(2) }}

    {{ super() | indent(2) }}

    if (fd >= 0 && fd < IC_FD_STATES_SIZE) {
      ic_fd_states[fd].read = true;
    }

    {{ release_lock_if_needed() | indent(2) }}
  }
### endblock send_msg
