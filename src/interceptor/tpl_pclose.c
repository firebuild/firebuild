{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the pclose() call.                                    #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block before
  /* save it here, we can't do fileno() after the pclose() */
  int fd = safe_fileno(stream);
  if (i_am_intercepting) {
    /* Send a synthetic close before the pclose() to avoid a deadlock in wait4. */
    FBB_Builder_close ic_msg;
    fbb_close_init(&ic_msg);
    fbb_close_set_fd(&ic_msg, fd);
    fb_fbb_send_msg(&ic_msg, fb_sv_conn);
  }
### endblock before

