{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for methods that we don't intercept.                      #}
{# This is a standalone template, does not extend tpl.c.              #}
{# ------------------------------------------------------------------ #}
### if gen == 'decl.h'
#define ic_orig_{{ func }} {{ func }}
### endif
