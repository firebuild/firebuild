{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the readlink() family.                                #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block set_fields
    {{ super() }}
    if (ret >= 0 && (size_t)abs(ret) <= bufsiz) {
      char *ret_path = strndup(buf, ret);
      m->set_ret_path(ret_path);
      free(ret_path);
    }
### endblock set_fields
