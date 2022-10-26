{# ------------------------------------------------------------------ #}
{# Copyright (c) 2022 Firebuild Inc.                                  #}
{# All rights reserved.                                               #}
{# Free for personal use and commercial trial.                        #}
{# Non-trial commercial use requires licenses available from          #}
{# https://firebuild.com                                              #}
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

### block call_orig
  /* Treating the optional parameter as 'void *' should work, see #178. */
  void *voidp_arg = va_arg(ap, void *);
  ret = ic_orig_{{ func }}(fd, cmd, voidp_arg);
### endblock call_orig
