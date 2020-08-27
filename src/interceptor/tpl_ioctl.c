{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the ioctl() call.                                     #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

{% set send_msg_condition = "to_send" %}

### block before
  /* Preparations */
  bool to_send = false;

  switch (cmd) {
    /* Commands that don't take an arg (or the arg doesn't matter to
     * the supervisor), but the supervisor needs to know about. */
    case FIOCLEX:
    case FIONCLEX: {
      to_send = true;
      break;
    }

    /* Commands the supervisor doesn't need to know about. There are way
     * too many to list them all, so just use the wildcard. */
    default: {
      break;
    }
  }
### endblock before
