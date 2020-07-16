{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the dlopen() family.                                  #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

{% set msg_add_fields = ["if (absolute_filename != NULL) m->set_absolute_filename(absolute_filename);"] %}

### block after
  char *absolute_filename = NULL;
  if (ret != NULL) {
    struct link_map *map;
    if (dlinfo(ret, RTLD_DI_LINKMAP, &map) == 0) {
      absolute_filename = map->l_name;
    }
  }
### endblock after
