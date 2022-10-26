{# ------------------------------------------------------------------ #}
{# Copyright (c) 2022 Firebuild Inc.                                  #}
{# All rights reserved.                                               #}
{# Free for personal use and commercial trial.                        #}
{# Non-trial commercial use requires licenses available from          #}
{# https://firebuild.com                                              #}
{# ------------------------------------------------------------------ #}
{# Template for methods where we only insert a trace marker, but      #}
{# other than that do not intercept (no locking or anything).         #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

{% set msg = None %}
{% set global_lock = 'never' %}

### block no_intercept
  i_am_intercepting = false;
### endblock no_intercept
