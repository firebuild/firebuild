{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the close() family.                                   #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

###       block send_msg
###         if msg
  /* Maybe notify the supervisor */
  if (i_am_intercepting && {{ send_msg_condition }}) {

    if (was_pipe) {
      /* We are going to use the socket for this message if closing a pipe.
       * Make sure there's no pending message in shmq. Do this by sending a barrier
       * (an empty ACK'ed message) over shmq and waiting for its ACK, if needed. */
      if (!shmq_writer_queue_is_empty(&fb_shmq)) {
        FBBCOMM_Builder_barrier ic_msg_barrier;
        fbbcomm_builder_barrier_init(&ic_msg_barrier);
        fb_fbbcomm_send_msg_and_check_ack_shmq(&ic_msg_barrier);
      }
    }

    FBBCOMM_Builder_{{ msg }} ic_msg;
    fbbcomm_builder_{{ msg }}_init(&ic_msg);

###           block set_fields
    /* Auto-generated from the function signature */
###             for (type, name) in types_and_names
###               if name not in msg_skip_fields
    fbbcomm_builder_{{ msg }}_set_{{ name }}(&ic_msg, {{ name }});
###               else
    /* Skipping '{{ name }}' */
###               endif
###             endfor
###             if msg_add_fields
    /* Additional ones from 'msg_add_fields' */
###               for item in msg_add_fields
    {{ item }}
###               endfor
###             endif
###           endblock set_fields

###           if send_ret_on_success
    /* Send return value on success */
    if (success) fbbcomm_builder_{{ msg }}_set_ret(&ic_msg, ret);
###           else
    /* Not sending return value */
###           endif

###           if send_msg_on_error
    /* Send errno on failure */
###             if not no_saved_errno
    if (!success) fbbcomm_builder_{{ msg }}_set_error_no(&ic_msg, saved_errno);
###             else
    if (!success) fbbcomm_builder_{{ msg }}_set_error_no(&ic_msg, errno);
###             endif
###           endif

    if (was_pipe) {
      /* Closure of a pipe needs to go over socket and handled by libevent on the supervisor because
       * it modifies libevent's set of watched fds. Wait for an ACK to make sure messages over the
       * two channels don't mix up. */
      fb_fbbcomm_send_msg_and_check_ack_socket(&ic_msg);
    } else {
      /* Non-pipe close can go over shmq, no ACK needed. */
      fb_fbbcomm_send_msg_shmq(&ic_msg);
    }
  }
###         endif
###       endblock send_msg
