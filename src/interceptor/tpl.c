{# ------------------------------------------------------------------ #}
{# Copyright (c) 2022 Firebuild Inc.                                  #}
{# All rights reserved.                                               #}
{# Free for personal use and commercial trial.                        #}
{# Non-trial commercial use requires licenses available from          #}
{# https://firebuild.com.                                             #}
{# Modification and redistribution are permitted, but commercial use  #}
{# of derivative works is subject to the same requirements of this    #}
{# license.                                                           #}
{# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,    #}
{# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF #}
{# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND              #}
{# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT        #}
{# HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,       #}
{# WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, #}
{# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER      #}
{# DEALINGS IN THE SOFTWARE.                                          #}
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
{#  ifdef_guard          #if or #ifdef guard wrapping declarations,   #}
{#                       definitions and other func related parts     #}
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
{# Send it in case of error too, but not on EFAULT or EINTR, see #713 and #723. #}
###     set send_msg_condition = "success || (errno != EINTR && errno != EFAULT)"
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
###   if ifdef_guard
{{ ifdef_guard }}
###   endif
###   block decl_h
###     if not syscall
###       if target == "darwin"
#define get_ic_orig_{{ func }}() {{ func }}
extern {{ rettype }} {{ func }}({{ sig_str }});
###       else
extern {{ rettype }} (*get_ic_orig_{{ func }}(void)) ({{ sig_str }});
###       endif
###     else
#define ic_orig_{{ func }}(...) get_ic_orig_syscall()({{ func }} __VA_OPT__(,) __VA_ARGS__)
###     endif

###   endblock decl_h
###   if ifdef_guard
#endif
###   endif
### endif
{#                                                                    #}
{# --- Template for 'def.c' ----------------------------------------- #}
{#                                                                    #}
### if gen == 'def.c'
###   if ifdef_guard
{{ ifdef_guard }}
###   endif
###   block def_c
###     if not syscall
###       if target == "darwin"
{{ rettype }} interposing_{{ func }} ({{ sig_str }});
static struct {const void* new; const void* original;} interpose_map_{{ func }} __attribute__ ((used, section("__DATA, __interpose"))) =
  {(const void*)interposing_{{ func }}, (const void*){{ func }}};
###       else
inline {{ rettype }} (*get_ic_orig_{{ func }}(void)) ({{ sig_str }}) {
  static {{ rettype }}(*resolved)({{ sig_str }}) = NULL;
  if (resolved == NULL) {
    resolved = dlsym(RTLD_NEXT, "{{ func }}");
  }
  return resolved;
}
###       endif
###     endif
###   endblock def_c
###   if ifdef_guard
#endif
###   endif
### endif
{#                                                                    #}
{# --- Template for 'reset.c' --------------------------------------- #}
{#                                                                    #}
### if gen == 'reset.c'
###   if ifdef_guard
{{ ifdef_guard }}
###   endif
###   block reset_c
###   endblock reset_c
###   if ifdef_guard
#endif
###   endif
### endif
{#                                                                    #}
{# --- Template for 'list.txt' -------------------------------------- #}
{#                                                                    #}
### if gen == 'list.txt'
{# Since ifdef_guard-s are not working here list.txt may have         #}
{# duplicates.                                                        #}
###   block list_txt
###     if not syscall
###       if target == "darwin"
_{{ func }}
###       else
{{ func }}
###       endif
###     endif
###   endblock list_txt
### endif
{#                                                                    #}
{# --- Template for 'impl.c' and 'impl_syscalls.c.inc' -------------- #}
{#                                                                    #}
{# If func does not begin with 'SYS_' then it is an actual libc       #}
{# function (perhaps a thin wrapper around a kernel syscall).         #}
{# We generate a complete function definition into 'impl.c'.          #}
{#                                                                    #}
{# If func begins with 'SYS_' then it denotes the first parameter of  #}
{# a syscall(). We generate a 'case' label into 'impl_syscalls.c.inc' #}
{# which is to be '#include'd within a 'switch' statement.            #}
{#                                                                    #}
### if gen in ['impl.c', 'impl_syscalls.c.inc']

###   macro grab_lock_if_needed(grab_condition)
  /* Grabbing the global lock (unless it's already ours, e.g. we're in a signal handler) */
  bool i_locked = false;  /* "i" as in "me, myself and I" */
  if (i_am_intercepting && ({{ grab_condition }})) {
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

###     if ifdef_guard
{{ ifdef_guard }}
###     endif

###     if not syscall
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

###       if target == "darwin"
{{ rettype }} {{ func }} ({{ sig_str }});
{{ rettype }} interposing_{{ func }} ({{ sig_str }}) {
###       else
{{ rettype }} {{ func }} ({{ sig_str }}) {
###       endif
###     else
#ifdef {{ func }}  /* this is prone against typos in the syscall name, but handles older kernels */
case {{ func }}: {
#define IC_SYSCALL_{{ func }}_IS_INTERCEPTED
/* The 64 bit variant has to be defined earlier. */
###       if not func.endswith("64")
#if defined {{ func }}64 && !defined IC_SYSCALL_{{ func }}64_IS_INTERCEPTED
#error "Missing {{ func }}64 interception"
#endif
###       endif
  va_list ap_args;
  va_start(ap_args, number);
###       for arg in args
###         if arg['vatype'] == "mode_t"
###           if target == "darwin"
  int {{ arg['name'] }} = va_arg(ap_args, int);
###           else
  {{ arg['vatype_and_name'] }} = va_arg(ap_args, {{ arg['vatype'] }});
###           endif
###         else
  {{ arg['vatype_and_name'] }} = va_arg(ap_args, {{ arg['vatype'] }});
###         endif
###       endfor
  va_end(ap_args);

###     endif

###     if rettype != 'void'
  {{ rettype }} ret;
###     endif

  /* Maybe don't intercept? */
###     block intercept
  /* use a copy, in case another thread modifies it */
###       if target == "darwin"
  /* On Darwin the libc calls out to intercepted functions, thus intercept only
   * the first libc entry point. */
  bool i_am_intercepting = intercepting_enabled
                           && (!FB_THREAD_LOCAL(intercept_on)
                               || FB_THREAD_LOCAL(interception_recursion_depth) > 0);
###       else
  bool i_am_intercepting = intercepting_enabled;
###       endif
  (void)i_am_intercepting;  /* sometimes it's unused, silence warning */
###     endblock intercept

  /* Guard the communication channel */
###     block guard_connection_fd
###       for arg in args
{# It is ugly to check for the variable name to end with "fd", but is simple and works well in practice. #}
###         if arg['type'] == "int" and arg['name'][-2:] == "fd"
  if ({{ arg['name'] }} == fb_sv_conn) { errno = EBADF; return {% if '*' in rettype %}NULL{% else %}-1{% endif %}; }
###         endif
###       endfor
###     endblock

###     if vararg
  /* Auto-generated for vararg functions */
###       if not syscall
  va_list ap;
  va_start(ap, {{ args[-1]['name'] }});
###       else
  va_list ap;
  va_start(ap, number);
###         for arg in args
  va_arg(ap_args, {{ arg['type'] }});  /* consume {{ arg['name'] }} */
###         endfor
###       endif
###     endif

  /* Warm up */
###     if not no_saved_errno
  int saved_errno = errno;
###     endif

  if (i_am_intercepting && !ic_init_done) fb_ic_init();

#ifdef FB_EXTRA_DEBUG
  if (insert_trace_markers) {
    char debug_buf[256];
    snprintf(debug_buf, sizeof(debug_buf), "%s%s{{ debug_before_fmt }}",
        i_am_intercepting ? "" : "[not intercepting] ",
        "{{ func }}"{{ debug_before_args }});
    insert_begin_marker(debug_buf);
  }
#endif

###     block grab_lock
###       if global_lock == 'before'
  {{ grab_lock_if_needed('i_am_intercepting') }}
###       endif
###     endblock grab_lock

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
  {{ call_ic_orig_func }}({{ names_str }});
###           else
#error "Need to implement call_orig for vararg function {{ func }}()"
###           endif
###         endif
###       endblock call_orig
###       if not no_saved_errno
  saved_errno = errno;
###       endif
  success = ({{ success }});
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
  if (i_am_intercepting && ({{ send_msg_condition }})) {
    FBBCOMM_Builder_{{ msg }} ic_msg;
    fbbcomm_builder_{{ msg }}_init(&ic_msg);

###           block set_fields
    /* Auto-generated from the function signature */
###             for arg in args
###               if not msg_skip_fields or arg['name'] not in msg_skip_fields
    fbbcomm_builder_{{ msg }}_set_{{ arg['name'] }}(&ic_msg, {{ arg['name'] }});
###               else
    /* Skipping '{{ arg['name'] }}' */
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
###             if not msg_skip_fields or 'error_no' not in msg_skip_fields
###               if not no_saved_errno
    if (!success) fbbcomm_builder_{{ msg }}_set_error_no(&ic_msg, saved_errno);
###               else
    if (!success) fbbcomm_builder_{{ msg }}_set_error_no(&ic_msg, errno);
###               endif
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
#ifdef FB_EXTRA_DEBUG
  if (insert_trace_markers) {
    char debug_buf[256];
    snprintf(debug_buf, sizeof(debug_buf), "%s%s{{ debug_after_fmt }}",
        i_am_intercepting ? "" : "[not intercepting] ",
        "{{ func }}"{{ debug_after_args }});
    insert_end_marker(debug_buf);
  }
#endif
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
###     if not syscall
#pragma GCC diagnostic pop
#pragma GCC visibility pop
###     else
break;
#endif  /* {{ func }} */
###     endif

###     if ifdef_guard
#endif
###     endif

###   endblock impl_c
### endif
{#                                                                    #}
{# ------------------------------------------------------------------ #}
