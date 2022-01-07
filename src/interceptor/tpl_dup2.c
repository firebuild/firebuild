{# ------------------------------------------------------------------ #}
{# Copyright (c) 2022 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the dup2() and dup3() calls.                          #}
{# See issue #632 for detailed explanation.                           #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block guard_connection_fd
  /* Only handle oldfd here, newfd is handled a bit later. */
  if (oldfd == fb_sv_conn) { errno = EBADF; return -1; }
### endblock

### block before
  int fb_sv_conn_new = -1;
  if (newfd == fb_sv_conn) {
    /* In order to make this dup2() or dup3() actually happen to the desired newfd
     * and still be able to talk to the supervisor,
     * we need to move fb_sv_conn to some other file descriptor. */
    fb_sv_conn_new = ic_orig_dup(fb_sv_conn);
    if (fb_sv_conn_new < 0) {
      /* This dup() failed, which is very unlikely (out of available fds).
       * There's no hope to succeed with the actual dup2() and still be able to talk
       * to the supervisor. So just bail out. */
      if (i_locked) {
        release_global_lock();
      }
      errno = EBADF;
      return -1;
    }
    /* The communication fd has the close-on-exec flag set, and dup() doesn't copy it. */
    ic_orig_fcntl(fb_sv_conn_new, F_SETFD, FD_CLOEXEC);
  }
### endblock

### block after
  if (newfd == fb_sv_conn) {
    if (success) {
      /* The actual dup2() succeeded and thus automatically closed fb_sv_conn.
       * Use the new fd number from now on for the communication. */
      fb_sv_conn = fb_sv_conn_new;
    } else {
      /* The actual dup2() failed for whatever reason. Close the dupped connection fd. */
      ic_orig_close(fb_sv_conn_new);
    }
  }

  copy_notify_on_read_write_state(newfd, oldfd);
### endblock
