{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the dlopen() family.                                  #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

{% set msg_add_fields = ["if (absolute_filename != NULL) fbbcomm_builder_" + msg + "_set_absolute_filename(&ic_msg, absolute_filename);"] %}

### block before
  thread_libc_nesting_depth++;
### endblock before

### block after
  thread_libc_nesting_depth--;

  char *absolute_filename = NULL;
  if (ret != NULL) {
    struct link_map *map;
    if (dlinfo(ret, RTLD_DI_LINKMAP, &map) == 0) {
      absolute_filename = map->l_name;
    }
  }
### endblock after
