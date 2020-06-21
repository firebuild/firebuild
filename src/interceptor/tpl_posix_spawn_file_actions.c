{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the posix_spawn_file_actions_...() family.            #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block after
  if (success) {
    {{ func | replace("posix_spawn_file_actions_", "psfa_") }} ({{ names_str }});
  }
### endblock after

### block send_msg
  /* No supervisor communication */
### endblock send_msg
