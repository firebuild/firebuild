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
{# Template for the syscall() call.                                   #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block impl_c

/* Make the intercepting function visible */
#pragma GCC visibility push(default)
#pragma GCC diagnostic push

long {{ func }} ({{ sig_str }}) {
  switch (number) {

#include "interceptor/gen_impl_syscalls.c.inc"

    default: {
#ifdef FB_EXTRA_DEBUG
      if (insert_trace_markers) {
        char debug_buf[256];
        snprintf(debug_buf, sizeof(debug_buf), "%s%s{{ debug_before_fmt }}",
            "[not intercepting] ",
            "{{ func }}"{{ debug_before_args }});
        insert_begin_marker(debug_buf);
      }
#endif

      /* Pass on several long parameters unchanged, see #178. */
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
      long ret = ic_orig_{{ func }}(number, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);

#ifdef FB_EXTRA_DEBUG
      if (insert_trace_markers) {
        char debug_buf[256];
        snprintf(debug_buf, sizeof(debug_buf), "%s%s{{ debug_after_fmt }}",
            "[not intercepting] ",
            "{{ func }}"{{ debug_after_args }});
        insert_end_marker(debug_buf);
      }
#endif

      return ret;
    }
  }
}

#pragma GCC visibility pop

### endblock impl_c
