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
{# Template for the signal() [ANSI C], sigset() [System V] and        #}
{# sigaction() [POSIX] calls.                                         #}
{# sigvec() [BSD] is not included because glibc dropped this API.     #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block call_orig
###   if func in ['signal', 'SYS_signal', 'sigset']
  if (signal_is_wrappable(signum)) {
###     if func in ['signal', 'sigset']
    sighandler_t old_orig_signal_handler = (sighandler_t)orig_signal_handlers[signum - 1];
###     else
    long int old_orig_signal_handler = (long int)orig_signal_handlers[signum - 1];
###     endif
    sighandler_t new_signal_handler =
        (handler == SIG_IGN || handler == SIG_DFL) ? handler : wrapper_signal_handler_1arg;
    orig_signal_handlers[signum - 1] = (void (*)(void))handler;
    ret = {{ call_ic_orig_func }}(signum, new_signal_handler);
    if ((void (*)(int))ret == wrapper_signal_handler_1arg) {
      ret = old_orig_signal_handler;
    }
  } else {
    ret = {{ call_ic_orig_func }}(signum, handler);
  }
###   elif func in ['sigaction', 'SYS_sigaction']
  if (signal_is_wrappable(signum)) {
    struct sigaction wrapped_act;
    void (*old_orig_signal_handler)(void) = orig_signal_handlers[signum - 1];
    if (act != NULL) {
      wrapped_act = *act;
      if (act->sa_flags & SA_SIGINFO) {
        /* sa_sigaction, handler called with 3 args */
        orig_signal_handlers[signum - 1] = (void (*)(void))act->sa_sigaction;
        /* FIXME(egmont) It's unclear to me whether SIG_IGN and SIG_DFL are allowed values here in the SA_SIGINFO branch,
         * probably not (they're of incompatible pointer types, hence the double casting). Still, better safe than sorry. */
        void (*new_signal_handler)(int, siginfo_t *, void *) =
            ((sighandler_t)(void *)act->sa_sigaction == SIG_IGN || (sighandler_t)(void *)act->sa_sigaction == SIG_DFL) ? act->sa_sigaction : wrapper_signal_handler_3arg;
        wrapped_act.sa_sigaction = new_signal_handler;
      } else {
        /* sa_handler, handler called with 1 arg */
        orig_signal_handlers[signum - 1] = (void (*)(void))wrapped_act.sa_handler;
        void (*new_signal_handler)(int) =
            (act->sa_handler == SIG_IGN || act->sa_handler == SIG_DFL) ? act->sa_handler : wrapper_signal_handler_1arg;
        wrapped_act.sa_handler = new_signal_handler;
      }
    }
    ret = {{ call_ic_orig_func }}(signum, act ? &wrapped_act : NULL, oldact);
    if (ret == 0 && oldact != NULL) {
      if (oldact->sa_flags & SA_SIGINFO) {
        /* sa_sigaction, handler called with 3 args */
        if (oldact->sa_sigaction == wrapper_signal_handler_3arg) {
          oldact->sa_sigaction = (void (*)(int, siginfo_t *, void *))old_orig_signal_handler;
        }
      } else {
        /* sa_handler, handler called with 1 arg */
        if (oldact->sa_handler == wrapper_signal_handler_1arg) {
          oldact->sa_handler = (void (*)(int))old_orig_signal_handler;
        }
      }
    }
  } else {
    ret = get_ic_orig_sigaction()(signum, act, oldact);
  }
###   endif
### endblock call_orig

### block send_msg
  /* Shhhhh, don't tell anything to the supervisor */
### endblock send_msg
