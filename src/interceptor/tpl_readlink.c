{# ------------------------------------------------------------------ #}
{# Copyright (c) 2022 Firebuild Inc.                                  #}
{# All rights reserved.                                               #}
{# Free for personal use and commercial trial.                        #}
{# Non-trial commercial use requires licenses available from          #}
{# https://firebuild.com                                              #}
{# ------------------------------------------------------------------ #}
{# Template for the readlink() family.                                #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block set_fields
    {{ super() }}
    /* Create a zero-terminated copy on the stack.
     * Make sure it lives until we send the message. */
    int len = 0;
    if (ret >= 0 && (size_t)labs(ret) <= bufsiz) {
      len = ret;
    }
    char ret_target[len + 1];
    if (len > 0) {
      memcpy(ret_target, buf, len);
      ret_target[len] = '\0';
      /* Returned path is a raw string, not to be resolved. */
      fbbcomm_builder_{{ msg }}_set_ret_target(&ic_msg, ret_target);
    }
### endblock set_fields
