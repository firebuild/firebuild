{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# This is the base template file that other templates derive from.   #}
{# In the mean time, this template is suitable for generating the     #}
{# required code for the majority of the intercepted functions.       #}
{# ------------------------------------------------------------------ #}
{# Parameters:                                                        #}
{#  global_lock:         If to acquire global lock (default: true)    #}
{#  before_lines:        Things to place right before the call        #}
{#  call_orig_lines:     How to call the orig method                  #}
{#  after_lines:         Things to place right after the call         #}
{#  success:             Success condition (default: "ret >= 0")      #}
{#  msg_skip_fields:     Don't automatically set these fields         #}
{#  msg_add_fields:      Additional code lines to set fields          #}
{#  send_ret_on_success: Whether to send the actual return value      #}
{#                       on success (default: false)                  #}
{#  send_msg_on_error:   Whether to send the message (with errno) on  #}
{#                       error (default: true) or only report success #}
{#  send_msg_condition:  Custom condition to send message             #}
{#  ack:                 Whether to ask for ack (default: false)      #}
{# ------------------------------------------------------------------ #}
{# Jinja lacks native support for generating multiple files.          #}
{# Work it around by running multiple times, each time with a         #}
{# different value of `gen`, thus processing a different "segment"    #}
{# of this file.                                                      #}
{# ------------------------------------------------------------------ #}
{#                                                                    #}
{# Convenient handling of default-true boolean #}
### if send_msg_on_error is not defined
###   set send_msg_on_error = true
### endif
### if not send_msg_condition
###   if send_msg_on_error
###     set send_msg_condition = "true"
###   else
###     set send_msg_condition = "success"
###   endif
### endif
### if global_lock is not defined
###   set global_lock = true
### endif
{#                                                                    #}
{# --- Template for 'decl.h' ---------------------------------------- #}
{#                                                                    #}
### if gen == 'decl.h'
###   block decl_h
extern {{ rettype }} (*ic_orig_{{ func }}) ({{ sig_str }});
###   endblock decl_h
### endif
{#                                                                    #}
{# --- Template for 'def.c' ----------------------------------------- #}
{#                                                                    #}
### if gen == 'def.c'
###   block decl_c
{{ rettype }} (*ic_orig_{{ func }}) ({{ sig_str }});
###   endblock decl_c
### endif
{#                                                                    #}
{# --- Template for 'init.c' ---------------------------------------- #}
{#                                                                    #}
### if gen == 'init.c'
###   block init_c
ic_orig_{{ func }} = ({{ rettype }}(*)({{ sig_str }})) dlsym(RTLD_NEXT, "{{ func }}");
###   endblock init_c
### endif
{#                                                                    #}
{# --- Template for 'reset.c' --------------------------------------- #}
{#                                                                    #}
### if gen == 'reset.c'
###   block reset_c
###   endblock reset_c
### endif
{#                                                                    #}
{# --- Template for 'list.txt' -------------------------------------- #}
{#                                                                    #}
### if gen == 'list.txt'
###   block list_txt
{{ func }}
###   endblock list_txt
### endif
{#                                                                    #}
{# --- Template for 'impl.c' ---------------------------------------- #}
{#                                                                    #}
### if gen == 'impl.c'

###   macro grab_lock_if_needed(grab_condition)
  /* Grabbing the global lock (unless it's already ours, e.g. we're in a signal handler) */
  bool i_locked = false;  /* "i" as in "me, myself and I" */
  if ({{ grab_condition }}) {
    thread_signal_danger_zone_enter();

    /* Some internal integrity assertions */
    if ((thread_has_global_lock) != (thread_intercept_on != NULL)) {
      char debug_buf[256];
      snprintf(debug_buf, sizeof(debug_buf), "Internal error while intercepting %s: thread_has_global_lock (%s) and thread_intercept_on (%s) must go hand in hand", "{{ func }}", thread_has_global_lock ? "true" : "false", thread_intercept_on);
      insert_debug_msg(debug_buf);
      assert(0 && "Internal error: thread_has_global_lock and thread_intercept_on must go hand in hand");
    }
    if (thread_signal_handler_running_depth == 0 && thread_libc_nesting_depth == 0 && thread_intercept_on != NULL) {
      char debug_buf[256];
      snprintf(debug_buf, sizeof(debug_buf), "Internal error while intercepting %s: already intercepting %s (and no signal or atfork handler running in this thread)", "{{ func }}", thread_intercept_on);
      insert_debug_msg(debug_buf);
      assert(0 && "Internal error: nested interceptors (no signal handler running)");
    }

    if (!thread_has_global_lock) {
      pthread_mutex_lock(&ic_global_lock);
      thread_has_global_lock = true;
      thread_intercept_on = "{{ func }}";
      i_locked = true;
    }
    thread_signal_danger_zone_leave();
    assert(thread_signal_danger_zone_depth == 0);
  }
  /* Global lock grabbed */
###   endmacro

###   macro release_lock_if_needed()
  /* Releasing the global lock (if we grabbed it in this pass) */
  if (i_locked) {
    thread_signal_danger_zone_enter();
    pthread_mutex_unlock(&ic_global_lock);
    thread_has_global_lock = false;
    thread_intercept_on = NULL;
    thread_signal_danger_zone_leave();
    assert(thread_signal_danger_zone_depth == 0);
  }
  /* Global lock released */
###   endmacro

/* Generated from {{ tpl }} */
###   block impl_c

/* Make the intercepting function visible */
#pragma GCC visibility push(default)

{{ rettype }} {{ func }} ({{ sig_str }}) {
###     if rettype != 'void'
  {{ rettype }} ret;
###     endif
  bool i_am_intercepting = intercepting_enabled;  /* use a copy, in case another thread modifies it */

  /* Guard the communication channel */
###     block guard_connection_fd
###       for (type, name) in types_and_names
{# It is ugly to check for the variable name to end with "fd", but is simple and works well in practice. #}
###         if type == "int" and name[-2:] == "fd"
  if ({{ name }} == fb_sv_conn) { errno = EBADF; return -1; }
###         endif
###       endfor
###     endblock

###     if vararg
  /* Auto-generated for vararg functions */
  va_list ap;
  va_start(ap, {{ names[-1] }});
###     endif

  /* Maybe don't intercept? */
###     block no_intercept
###     endblock no_intercept

  /* Warm up */
###     if not no_saved_errno
  int saved_errno = errno;
###     endif

  if (!ic_init_done) fb_ic_load();

  if (insert_trace_markers) {
    char debug_buf[256];
    snprintf(debug_buf, sizeof(debug_buf), "%s%s{{ debug_before_fmt }}",
        i_am_intercepting ? "" : "[not intercepting] ",
        "{{ func }}"{{ debug_before_args }});
    insert_begin_marker(debug_buf);
  }

###     if global_lock
  {{ grab_lock_if_needed('i_am_intercepting') }}
###     endif

###     block body
  bool success = false;

  /* Beforework */
###       block before
###         if before_lines
###           for item in before_lines
  {{ item }}
###           endfor
###         endif
###       endblock before

  /* Perform the call */
###       if not no_saved_errno
  errno = saved_errno;
###       endif
###       block call_orig
###         if call_orig_lines
###           for item in call_orig_lines
  {{ item }}
###           endfor
###         else
###           if not vararg
  {%+ if rettype != 'void' %}ret = {% endif -%}
  ic_orig_{{ func }}({{ names_str }});
###           else
  void *args = __builtin_apply_args();
  {%+ if rettype != 'void' %}void const * const result ={% endif -%}
  __builtin_apply((void *) ic_orig_{{ func }}, args, 100);
  {%+ if rettype != 'void' %}ret = *({{ rettype }}*)result;{% endif %}

###           endif
###         endif
###       endblock call_orig
###       if not no_saved_errno
  saved_errno = errno;
###       endif
  success = {{ success }};
  (void)success;  /* sometimes it's unused, silence warning */

  /* Afterwork */
###       block after
###         if after_lines
###           for item in after_lines
  {{ item }}
###           endfor
###         endif
###       endblock after

###       block send_msg
###         if msg
  /* Maybe notify the supervisor */
  if (i_am_intercepting && {{ send_msg_condition }}) {
    FBB_Builder_{{ msg }} ic_msg;
    fbb_{{ msg }}_init(&ic_msg);

###           block set_fields
    /* Auto-generated from the function signature */
###             for (type, name) in types_and_names
###               if name not in msg_skip_fields
    fbb_{{ msg }}_set_{{ name }}(&ic_msg, {{ name }});
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
    if (success) fbb_{{ msg }}_set_ret(&ic_msg, ret);
###           else
    /* Not sending return value */
###           endif

###           if send_msg_on_error
    /* Send errno on failure */
###             if not no_saved_errno
    if (!success) fbb_{{ msg }}_set_error_no(&ic_msg, saved_errno);
###             else
    if (!success) fbb_{{ msg }}_set_error_no(&ic_msg, errno);
###             endif
###           endif

###           if ack
    /* Send and wait for ack */
    fb_fbb_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
###           else
    /* Send and go on, no ack */
    fb_fbb_send_msg(&ic_msg, fb_sv_conn);
###           endif
  }
###         endif
###       endblock send_msg

###     endblock body

  /* Cool down */
  if (insert_trace_markers) {
    char debug_buf[256];
    snprintf(debug_buf, sizeof(debug_buf), "%s%s{{ debug_after_fmt }}",
        i_am_intercepting ? "" : "[not intercepting] ",
        "{{ func }}"{{ debug_after_args }});
    insert_end_marker(debug_buf);
  }

###     if global_lock
  {{ release_lock_if_needed() }}
###     endif

###     if not no_saved_errno
  errno = saved_errno;
###     endif

###     if vararg
  /* Auto-generated for vararg functions */
  va_end(ap);
###     endif

###     if rettype != 'void'
  return ret;
###     endif
}
#pragma GCC visibility pop


###   endblock impl_c
### endif
{#                                                                    #}
{# ------------------------------------------------------------------ #}
