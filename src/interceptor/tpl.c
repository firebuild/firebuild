{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# This is the base template file that other templates derive from.   #}
{# In the mean time, this template is suitable for generating the     #}
{# required code for the majority of the intercepted functions.       #}
{# ------------------------------------------------------------------ #}
{# Parameters:                                                        #}
{#  global_lock:         Whether to acquire the global lock 'before', #}
{#                       or 'after' the operation, or 'never'         #}
{#                       (default: 'before')                          #}
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
{#  ack_condition:       Whether to ask for ack 'true', 'false' or    #}
{#                       '<condition>' (default: 'false')             #}
{#  after_send_lines:    Things to place after sending msg            #}
{#  diagnostic_ignored:  GCC diagnostic ignored for the function      #}
{# ------------------------------------------------------------------ #}
{# Jinja lacks native support for generating multiple files.          #}
{# Work it around by running multiple times, each time with a         #}
{# different value of `gen`, thus processing a different "segment"    #}
{# of this file.                                                      #}
{# ------------------------------------------------------------------ #}
{#                                                                    #}
{# Convenient handling of default-true booleans and other defaults #}
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
###   set global_lock = 'before'
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
    grab_global_lock(&i_locked, "{{ func }}");
  }
  /* Global lock grabbed */
###   endmacro

###   macro release_lock_if_needed()
  /* Releasing the global lock (if we grabbed it in this pass) */
  if (i_locked) {
    release_global_lock();
  }
  /* Global lock released */
###   endmacro

/* Generated from {{ tpl }} */
###   block impl_c

/* Make the intercepting function visible */
#pragma GCC visibility push(default)
#pragma GCC diagnostic push

###         if diagnostic_ignored
###           for item in diagnostic_ignored
#pragma GCC diagnostic ignored "{{ item }}"
###           endfor
###         endif

/* Undefine potential macro */
#ifdef {{ func }}
#undef {{ func }}
#endif

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

###     if global_lock == 'before'
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

###     if global_lock == 'after'
  {{ grab_lock_if_needed('i_am_intercepting') }}
###     endif

###       block send_msg
###         if msg
  /* Maybe notify the supervisor */
  if (i_am_intercepting && {{ send_msg_condition }}) {
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
###           if ack_condition
    /* Sending ack is conditional */
    if ({{ ack_condition }}) {
      /* Send and wait for ack */
      fb_fbbcomm_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
    } else {
      /* Send and go on, no ack */
      fb_fbbcomm_send_msg(&ic_msg, fb_sv_conn);
    }
###           else
    /* Send and go on, no ack */
    fb_fbbcomm_send_msg(&ic_msg, fb_sv_conn);
###           endif
  }
###         endif
###       endblock send_msg
###       if after_send_lines
###         for item in after_send_lines
  {{ item }}
###         endfor
###       endif

###     endblock body

  /* Cool down */
  if (insert_trace_markers) {
    char debug_buf[256];
    snprintf(debug_buf, sizeof(debug_buf), "%s%s{{ debug_after_fmt }}",
        i_am_intercepting ? "" : "[not intercepting] ",
        "{{ func }}"{{ debug_after_args }});
    insert_end_marker(debug_buf);
  }

###     if global_lock == 'before' or global_lock == 'after'
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
#pragma GCC diagnostic pop
#pragma GCC visibility pop


###   endblock impl_c
### endif
{#                                                                    #}
{# ------------------------------------------------------------------ #}
