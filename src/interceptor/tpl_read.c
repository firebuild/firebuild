{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for functions reading from a file.                        #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

{% set msg = "read" %}
{% set send_msg_condition = "(fd < 0 || fd >= IC_FD_STATES_SIZE || ic_fd_states[fd].read == false)" %}

### block send_msg
  {{ super() }}
  if (fd >=0 && fd < IC_FD_STATES_SIZE) {
    ic_fd_states[fd].read = true;
  }
### endblock send_msg
