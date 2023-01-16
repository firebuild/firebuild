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
{# Template for clone().                                              #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

{% set msg_skip_fields = ["fn", "stack", "arg"] %}

### block call_orig

### if syscall
  /* Need to extract 'flags'. See clone(2) NOTES about differences between architectures. */
#if defined(__s390__) || defined(__cris__)
  va_arg(ap, void*);  /* skip over 'stack' */
#endif
  unsigned long flags = va_arg(ap, unsigned long);
  // TODO(rbalint) decode flags from varargs and intercept syscall() variants, too
  bool intercepted_clone = false;
### else
  // TODO(rbalint) cover more flag combinations
  bool intercepted_clone = (flags == (CLONE_VFORK | SIGCHLD));
### endif
  uint16_t ack_num = 0;
  if (i_am_intercepting) {
    if (intercepted_clone) {
      FBBCOMM_Builder_fork_parent ic_msg;
      fbbcomm_builder_fork_parent_init(&ic_msg);
      ack_num = fb_fbbcomm_send_msg_with_ack(&ic_msg, fb_sv_conn);
    } else {
      pre_clone_disable_interception(flags, &i_locked);
    }
  }

### if not syscall
  int vararg_count = 0;
  if (flags & (CLONE_CHILD_CLEARTID | CLONE_CHILD_SETTID)) {
    vararg_count = 3;
  } else if (flags & CLONE_SETTLS) {
    vararg_count = 2;
  } else if (flags & (CLONE_PARENT_SETTID | CLONE_PIDFD)) {
    vararg_count = 1;
  }

  int (*passed_fn)(void *) = intercepted_clone ? clone_trampoline : fn;
  clone_trampoline_arg trampoline_arg = {fn, arg, i_locked};
  void *passed_arg = intercepted_clone ? &trampoline_arg : arg;
  if (vararg_count == 0) {
    ret = get_ic_orig_{{ func }}()(passed_fn, stack, flags, passed_arg);
  } else {
    pid_t *parent_tid = va_arg(ap, pid_t *);
    if (vararg_count == 1) {
      ret = get_ic_orig_{{ func }}()(passed_fn, stack, flags, passed_arg, parent_tid);
    } else {
      void *tls = va_arg(ap, void *);
      if (vararg_count == 2) {
        ret = get_ic_orig_{{ func }}()(passed_fn, stack, flags, passed_arg, parent_tid, tls);
      } else {
        pid_t *child_tid = va_arg(ap, pid_t *);
        ret = get_ic_orig_{{ func }}()(passed_fn, stack, flags, passed_arg, parent_tid, tls, child_tid);
      }
    }
  }
### else
  /* The order of parameters is heavily architecture dependent. */
  /* Pass on several long parameters unchanged, as in tpl_syscall.c. */
  va_list ap_pass;
  va_start(ap_pass, number);
  long arg1 = va_arg(ap_pass, long);
  long arg2 = va_arg(ap_pass, long);
  long arg3 = va_arg(ap_pass, long);
  long arg4 = va_arg(ap_pass, long);
  long arg5 = va_arg(ap_pass, long);
  long arg6 = va_arg(ap_pass, long);
  long arg7 = va_arg(ap_pass, long);
  long arg8 = va_arg(ap_pass, long);
  va_end(ap_pass);
  ret = ic_orig_{{ func }}(arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
### endif

### endblock call_orig

### block send_msg
  /* Notify the supervisor */
  if (!success) {
    if (intercepted_clone) {
      // TODO(rbalint) fix this case
      assert(0 && "The supervisor still waits for the child");
      fb_fbbcomm_check_ack(fb_sv_conn, ack_num);
    }
  } else if (ret == 0) {
    /* Child is running child_atfork_handler in clone_trampoline in the intercepted cases. */
  } else {
    if (intercepted_clone) {
      fb_fbbcomm_check_ack(fb_sv_conn, ack_num);
    }
  }

### endblock send_msg
