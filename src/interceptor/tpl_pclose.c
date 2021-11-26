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
    /* Make sure there's no pending message in shmq. Do this by sending a barrier
     * (an empty ACK'ed message) over shmq and waiting for its ACK, if needed. */
    if (!shmq_writer_queue_is_empty(&fb_shmq)) {
      FBBCOMM_Builder_barrier ic_msg_barrier;
      fbbcomm_builder_barrier_init(&ic_msg_barrier);
      fb_fbbcomm_send_msg_and_check_ack_shmq(&ic_msg_barrier);
    }
    /* Send a synthetic close before the pclose() to avoid a deadlock in wait4. */
    FBBCOMM_Builder_close ic_msg;
    fbbcomm_builder_close_init(&ic_msg);
    fbbcomm_builder_close_set_fd(&ic_msg, fd);
    /* An ACK here isn't necessary for the real business logic.
     * Wait for an ACK because most of the messages are sent over shmq, however this one, as it
     * manipulates the set of fds libevent listens for (since we're closing a pipe) needs to go over
     * socket, and we need to be sure that messages over the two channels keep their order. */
    fb_fbbcomm_send_msg_and_check_ack_socket(&ic_msg);
  }
### endblock before
