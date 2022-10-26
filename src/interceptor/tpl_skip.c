{# ------------------------------------------------------------------ #}
{# Copyright (c) 2022 Firebuild Inc.                                  #}
{# All rights reserved.                                               #}
{# Free for personal use and commercial trial.                        #}
{# Non-trial commercial use requires licenses available from          #}
{# https://firebuild.com                                              #}
{# ------------------------------------------------------------------ #}
{# Template for methods that we don't intercept.                      #}
{# This is a standalone template, does not extend tpl.c.              #}
{# ------------------------------------------------------------------ #}
### if gen == 'decl.h'
#define ic_orig_{{ func }} {{ func }}
### endif
