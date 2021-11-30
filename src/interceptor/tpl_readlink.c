{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
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
    char ret_path[len + 1];
    if (len > 0) {
      memcpy(ret_path, buf, len);
      ret_path[len] = '\0';
      /* Returned path is assumed to be canonical.*/
      fbbcomm_builder_{{ msg }}_set_ret_path(&ic_msg, ret_path);
    }
### endblock set_fields
