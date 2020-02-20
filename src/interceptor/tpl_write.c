{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for functions writing to a file.                          #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

{% set msg = "write" %}
{% set send_msg_condition = "(*fd_states)[fd].written == false" %}

### block before
  {{ super() }}
  pthread_mutex_lock(&ic_fd_states_lock);
  try {
    if (fd >= 0) fd_states->at(fd);
  } catch (std::exception& e) {
    fd_states->resize(fd + 1);
  }
### endblock before

### block send_msg
  {{ super() }}
  (*fd_states)[fd].written = true;
  pthread_mutex_unlock(&ic_fd_states_lock);
### endblock send_msg
