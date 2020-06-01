{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for functions writing to a file.                          #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

{% set msg = "write" %}
{% set send_msg_condition = "(fd < 0 || fd >= IC_FD_STATES_SIZE || ic_fd_states[fd].written == false)" %}

### block before
  {{ super() }}
  if (fd >= 0 && fd < IC_FD_STATES_SIZE) pthread_mutex_lock(&ic_fd_states_lock);
### endblock before

### block send_msg
  {{ super() }}
  if (fd >=0 && fd < IC_FD_STATES_SIZE) {
    ic_fd_states[fd].written = true;
    pthread_mutex_unlock(&ic_fd_states_lock);
  }
### endblock send_msg
