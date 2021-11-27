{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the vararg open() family.                             #}
{# (The non-vararg __open_2() variants are handled elsewhere.)        #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

{% set msg_add_fields = ["if (flags & O_CREAT) fbbcomm_builder_" + msg + "_set_mode(&ic_msg, mode);",
                         "BUILDER_SET_CANONICAL(" + msg + ", file);"] %}

### block before
  mode_t mode = 0;
  if (flags & O_CREAT) {
    mode = va_arg(ap, mode_t);
  }
### endblock before

### block call_orig
  ret = ic_orig_{{ func }}({{ names_str }}, mode);
### endblock call_orig
