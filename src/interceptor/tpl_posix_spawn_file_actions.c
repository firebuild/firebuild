{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the posix_spawn_file_actions_...() family.            #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block guard_connection_fd
{# Override the main template's corresponding block so that the       #}
{# connection fd is _not_ guarded here. This is because matching the  #}
{# raw fd number against the _current_ connection fd number is        #}
{# incorrect. By the time the actions we register here will be        #}
{# executed, the communication fd might have moved elsewhere due to   #}
{# an intercepted dup2(), or reopened as a regular file due to a      #}
{# preceding posix_spawn_file_action. See #875 for further details.   #}
### endblock

### block after
  if (success) {
    {{ func | replace("posix_spawn_file_actions_", "psfa_") }} ({{ names_str }});
  }
### endblock after

### block send_msg
  /* No supervisor communication */
### endblock send_msg
