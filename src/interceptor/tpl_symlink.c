{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the symlink() family.                                 #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

{% set msg_skip_fields = ["oldpath", "newpath"] %}
{% set msg_add_fields = ["BUILDER_SET_CANONICAL(" + msg + ", oldpath);",
                         "BUILDER_SET_CANONICAL(" + msg + ", newpath);"] %}
