{# ------------------------------------------------------------------ #}
{# Copyright (c) 2022 Firebuild Inc.                                  #}
{# All rights reserved.                                               #}
{# Free for personal use and commercial trial.                        #}
{# Non-trial commercial use requires licenses available from          #}
{# https://firebuild.com                                              #}
{# ------------------------------------------------------------------ #}
{# Template for the dlopen() family.                                  #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

{% set msg_add_fields = ["if (absolute_filename != NULL) BUILDER_SET_ABSOLUTE_CANONICAL(" + msg + ", absolute_filename);",
                         "fbbcomm_builder_dlopen_set_error(&ic_msg, !success);"] %}

### block before
  thread_libc_nesting_depth++;
### endblock before

### block after
  thread_libc_nesting_depth--;

  char *absolute_filename = NULL;
  if (success) {
    struct link_map *map;
    if (dlinfo(ret, RTLD_DI_LINKMAP, &map) == 0) {
      /* Note: contrary to the dlinfo(3) manual page, this is not necessarily absolute. See #657.
       * We'll resolve to absolute when setting the FBB field. */
      absolute_filename = map->l_name;
    } else {
      /* As per #920, dlinfo() returning an error _might_ cause problems later on in the intercepted
       * app, should it call dlerror(). A call to dlerror() would return a non-NULL string
       * describing dlinfo()'s failure, rather than NULL describing dlopen()'s success. But why
       * would any app invoke dlerror() after a successful dlopen()? Let's hope that in practice no
       * application does this. */
    }
  }
### endblock after
