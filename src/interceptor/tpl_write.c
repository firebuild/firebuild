{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for functions writing to a (regular or special) file,     #}
{# including                                                          #}
{# - low-level [p]write*() family                                     #}
{# - high-level stdio like fwrite(), putc(), printf(), perror() etc.  #}
{# - low-level socket writing send*() family                          #}
{# - ftruncate()                                                      #}
{# and perhaps more.                                                  #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### if is_pwrite is not defined
###   set is_pwrite = "false"
### endif

{% set msg = "write_to_inherited" %}
{# No locking around the write(): see issue #279 #}
{% set global_lock = 'never' %}

### block set_fields
  {{ super() }}
  fbbcomm_builder_{{ msg }}_set_is_pwrite(&ic_msg, is_pwrite);
### endblock set_fields

### block send_msg
  bool is_pwrite = {{ is_pwrite }};

  {# Acquire the lock if sending a message #}
  if (fd < 0 || fd >= IC_FD_STATES_SIZE ||
      (is_pwrite == false && ic_fd_states[fd].notify_on_write == true) ||
      (is_pwrite == true && ic_fd_states[fd].notify_on_pwrite == true)) {
    /* Need to notify the supervisor */

    {{ grab_lock_if_needed('true') | indent(2) }}

    {{ super() | indent(2) }}

    if (fd >= 0 && fd < IC_FD_STATES_SIZE) {
      ic_fd_states[fd].notify_on_write = false;
      if (is_pwrite) {
        ic_fd_states[fd].notify_on_pwrite = false;
      }
    }

    {{ release_lock_if_needed() | indent(2) }}
  }
### endblock send_msg
