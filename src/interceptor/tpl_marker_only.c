{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for methods where we only insert a trace marker, but      #}
{# other than that do not intercept (no locking or anything).         #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

{% set msg = None %}
{% set global_lock = False %}

### block no_intercept
  i_am_intercepting = false;
### endblock no_intercept
