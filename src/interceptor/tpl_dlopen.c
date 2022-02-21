{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the dlopen() family.                                  #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

{% set msg_add_fields = ["if (absolute_filename != NULL) BUILDER_SET_ABSOLUTE_CANONICAL(" + msg + ", absolute_filename);",
                         "if (!success) fbbcomm_builder_dlopen_set_error_string(&ic_msg, dlerror());"] %}

### block before
  thread_libc_nesting_depth++;
### endblock before

### block after
  thread_libc_nesting_depth--;

  char *absolute_filename = NULL;
  if (ret != NULL) {
    struct link_map *map;
    if (dlinfo(ret, RTLD_DI_LINKMAP, &map) == 0) {
      /* Note: contrary to the dlinfo(3) manual page, this is not necessarily absolute. See #657.
       * We'll resolve to absolute when setting the FBB field. */
      absolute_filename = map->l_name;
    }
  }
### endblock after
