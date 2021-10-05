{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for these calls:                                          #}
{#  - error(), error_at_line():                                       #}
{#      these call the atexit / on_exit handlers if status != 0       #}
{#  - err(), errx(), verr(), verrx():                                 #}
{#      these always call the atexit / on_exit handlers               #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block before
  /* First notify the supervisor that stderr has been written to,
   * similarly to tpl_write.c. */
  int fd = safe_fileno(stderr);
  if (i_am_intercepting && (fd < 0 || fd >= IC_FD_STATES_SIZE || ic_fd_states[fd].written == false)) {
    FBBCOMM_Builder_write ic_msg;
    fbbcomm_builder_write_init(&ic_msg);
    fbbcomm_builder_write_set_fd(&ic_msg, fd);
    fb_fbbcomm_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
  }
  if (fd >= 0 && fd < IC_FD_STATES_SIZE) {
    ic_fd_states[fd].written = true;
  }
### endblock before

### block call_orig
  /* Then call the original. If error()'s or error_at_line()'s status is non-zero,
   * or if the method is err(), errx(), verr(), verrx(), then the original will call exit()
   * and in turn the atexit / on_exit handlers, which can call intercepted functions.
   * So release the lock, just like in tpl_exit.c. */
###   if func in ['error', 'error_at_line']
  if (status == 0) {
    {{ super() }}
###   else
  if (false) {
###   endif
  } else {
    /* Exit handlers may call intercepted functions, so release the lock */
    thread_signal_danger_zone_enter();
    if (thread_has_global_lock) {
      pthread_mutex_unlock(&ic_global_lock);
      thread_has_global_lock = false;
      thread_intercept_on = NULL;
    }
    thread_signal_danger_zone_leave();
    assert(thread_signal_danger_zone_depth == 0);

    /* Mark the end now */
    insert_end_marker("{{ func }}");

    /* Perform the call.
     * This will call the registered atexit / on_exit handlers,
     * including our handle_exit() which will notify the supervisor. */
    {{ super() }}

    /* Make scan-build happy */
    (void)i_locked;

    /* Should not be reached */
###   if func in ['error', 'error_at_line']
    assert(0 && "{{ func }} with nonzero \"status\" parameter did not exit");
    abort(); /* for NDEBUG */
###   else
    assert(0 && "{{ func }} did not exit");
    abort(); /* for NDEBUG */
###   endif
  }
### endblock call_orig

### block send_msg
  /* Nothing else to tell to the supervisor */
### endblock send_msg
