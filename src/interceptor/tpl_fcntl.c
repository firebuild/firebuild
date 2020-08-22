{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the fcntl() family.                                   #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

{% set msg_add_fields = ["if (has_int_arg) fbb_" + msg + "_set_arg(&ic_msg, int_arg);"] %}
{% set send_msg_condition = "to_send" %}

### block before
  /* Preparations */
  bool to_send = false;
  bool has_int_arg = false;
  int int_arg;

  switch (cmd) {
    /* Commands the supervisor doesn't need to know about. */
    case F_GETFD:
    case F_GETFL:
    case F_SETFL:
    case F_GETLK:
    case F_SETLK:
    case F_SETLKW:
    case F_OFD_GETLK:
    case F_OFD_SETLK:
    case F_OFD_SETLKW:
    case F_GETOWN:
    case F_SETOWN:
    case F_GETOWN_EX:
    case F_SETOWN_EX:
    case F_GETSIG:
    case F_SETSIG:
    case F_GETLEASE:
    case F_SETLEASE:
    case F_NOTIFY:
    case F_GETPIPE_SZ:
    case F_SETPIPE_SZ:
    case F_ADD_SEALS:
    case F_GET_SEALS:
    case F_GET_RW_HINT:
    case F_SET_RW_HINT:
    case F_GET_FILE_RW_HINT:
    case F_SET_FILE_RW_HINT: {
      break;
    }

    /* Commands taking an int arg that the supervisor needs to know about. */
    case F_DUPFD:
    case F_DUPFD_CLOEXEC:
    case F_SETFD: {
      to_send = true;
      has_int_arg = true;
      int_arg = va_arg(ap, int);
      break;
    }

    /* Commands that don't take an arg (or the arg doesn't matter to
     * the supervisor), but the supervisor needs to know about. This
     * includes all the unrecognized commands. Let's spell out the
     * recognized ones, rather than just catching them by "default",
     * for better readability. */
    default: {
      to_send = true;
      break;
    }
  }
### endblock before
