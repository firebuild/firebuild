{# ------------------------------------------------------------------ #}
{# Copyright (c) 2022 Firebuild Inc.                                  #}
{# All rights reserved.                                               #}
{# Free for personal use and commercial trial.                        #}
{# Non-trial commercial use requires licenses available from          #}
{# https://firebuild.com                                              #}
{# ------------------------------------------------------------------ #}
{# Template for the closefrom() call.                                 #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block guard_connection_fd
  /* Skip our standard connection fd guarding. */
### endblock guard_connection_fd

### block call_orig
  /* Reset our file states for fds that will be closed. */
  if (i_am_intercepting) {
    int i;
    for (i = lowfd; i < IC_FD_STATES_SIZE; i++) {
      set_notify_on_read_write_state(i);
    }
  }

  if (lowfd > fb_sv_conn) {
    /* Just go ahead. */
    ic_orig_closefrom(lowfd);
  } else if (lowfd == fb_sv_conn) {
    /* Need to skip the first fd. */
    ic_orig_closefrom(lowfd + 1);
  } else {
    /* Need to leave a hole in the range. */
    ic_orig_close_range(lowfd, fb_sv_conn - 1, 0);
    ic_orig_closefrom(fb_sv_conn + 1);
  }
### endblock call_orig
