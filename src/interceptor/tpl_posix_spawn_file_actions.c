{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the posix_spawn_file_actions_...() family.            #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block guard_connection_fd
###   for (type, name) in types_and_names
{# It is ugly to check for the variable name to end with "fd", but is simple works well in practice. #}
###     if type == "int" and name[-2:] == "fd"
{# Unlike most methods which place the error code in errno and return -1, #}
{# posix_spawn_file_actions_add*() return the error code directly.        #}
{# FIXME Do we want to return an error here, or maybe pretend success?    #}
  if ({{ name }} == fb_sv_conn) return EBADF;
###     endif
###   endfor
### endblock
### block after
  if (success) {
    {{ func | replace("posix_spawn_file_actions_", "psfa_") }} ({{ names_str }});
  }
### endblock after

### block send_msg
  /* No supervisor communication */
### endblock send_msg
