{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# This is the base template file that other templates derive from.   #}
{# In the mean time, this template is suitable for generating the     #}
{# required code for the majority of the intercepted functions.       #}
{# ------------------------------------------------------------------ #}
{# Parameters:                                                        #}
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
{# Convenient handling of default-false boolean #}
### if send_msg_on_error is not defined
###   set send_msg_on_error = true
### endif
### if not send_msg_condition
###   if send_msg_on_error
###     set send_msg_condition = "1"
###   else
###     set send_msg_condition = "success"
###   endif
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
{# --- Template for 'impl.c' ---------------------------------------- #}
{#                                                                    #}
### if gen == 'impl.c'

/* Generated from {{ tpl }} */
###   block impl_c

/* Make the intercepting function visible */
#pragma GCC visibility push(default)

{{ rettype }} {{ func }} ({{ sig_str }}) {
###     if rettype != 'void'
  {{ rettype }} ret;
###     endif

  /* Warm up */
###     if not no_saved_errno
  int saved_errno = errno;
###     endif
  fb_ic_load();

  if (insert_trace_markers) {
    char debug_buf[1024];
    snprintf(debug_buf, 1000, "%s{{ debug_before_fmt }}", "{{ func }}"{{ debug_before_args }});
    insert_begin_marker(debug_buf);
  }
  if (intercept_on != NULL) {
    fprintf(stderr, "Started to intercept %s while already intercepting %s\n", "{{ func }}", intercept_on);
    assert(0);
  }
  intercept_on = "{{ func }}";

###     if vararg
  /* Auto-generated for vararg functions */
  va_list ap;
  va_start(ap, {{ names[-1] }});
###     endif

###     block body
  bool success = 0;

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
  __builtin_apply((void (*)(...))(void *)ic_orig_{{ func }}, args, 100);
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
  if ({{ send_msg_condition }}) {
    msg::InterceptorMsg ic_msg;
    auto m = ic_msg.mutable_{{ msg }}();

###           block set_fields
    /* Auto-generated from the function signature */
###             for (type, name) in types_and_names
###               if name not in msg_skip_fields
    {%+ if '*' in type %}if ({{ name }} != NULL) {% endif -%}
    m->set_{{ name }}({{ name }});
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
    if (success) m->set_ret(ret);
###           else
    /* Not sending return value */
###           endif

###           if send_msg_on_error
    /* Send errno on failure */
###             if not no_saved_errno
    if (!success) m->set_error_no(saved_errno);
###             else
    if (!success) m->set_error_no(errno);
###             endif
###           endif

###           if ack
    /* Send and wait for ack */
    fb_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
###           else
    /* Send and go on, no ack */
    fb_send_msg(ic_msg, fb_sv_conn);
###           endif
  }
###         endif
###       endblock send_msg

###     endblock body

###     if vararg
  /* Auto-generated for vararg functions */
  va_end(ap);
###     endif

  /* Cool down */
  if (insert_trace_markers) {
    char debug_buf[1024];
    snprintf(debug_buf, 1000, "%s{{ debug_after_fmt }}", "{{ func }}"{{ debug_after_args }});
    insert_end_marker(debug_buf);
  }
  intercept_on = NULL;
###     if not no_saved_errno
  errno = saved_errno;
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
