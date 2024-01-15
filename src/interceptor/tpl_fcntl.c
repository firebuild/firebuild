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
{# Template for the fcntl() family.                                   #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

{% set msg_add_fields = ["if (has_int_arg) fbbcomm_builder_" + msg + "_set_arg(&ic_msg, int_arg);",
                         "if (has_string_arg) fbbcomm_builder_" + msg + "_set_string_arg(&ic_msg, string_arg);",
                         "if (send_ret) fbbcomm_builder_" + msg + "_set_ret(&ic_msg, ret);"] %}
{% set send_msg_condition = "to_send" %}

### block before
  /* Preparations */
  bool to_send = false;
  bool send_ret = false;
  bool has_int_arg = false;
  int int_arg = -1;
  bool has_string_arg = false;
  char* string_arg = NULL;

  switch (cmd) {
    /* Commands the supervisor doesn't need to know about. */
    case F_GETFD:
    case F_GETFL:
    case F_SETFL:
    case F_GETLK:
    case F_SETLK:
    case F_SETLKW:
#ifdef __linux__
    case F_OFD_GETLK:
    case F_OFD_SETLK:
    case F_OFD_SETLKW:
#endif
    case F_GETOWN:
    case F_SETOWN:
#ifdef __linux__
    case F_GETOWN_EX:
    case F_SETOWN_EX:
    case F_GETSIG:
    case F_SETSIG:
    case F_GETLEASE:
    case F_SETLEASE:
    case F_NOTIFY:
    case F_GETPIPE_SZ:
    case F_SETPIPE_SZ:
    case F_ADD_SEALS:
    case F_GET_SEALS:
    case F_GET_RW_HINT:
    case F_SET_RW_HINT:
    case F_GET_FILE_RW_HINT:
    case F_SET_FILE_RW_HINT:
#endif
#ifdef __APPLE__
    case F_NOCACHE:
    case F_GETPROTECTIONCLASS:
#endif
      {
      break;
    }

    /* Commands taking an int arg that the supervisor needs to know about,
     * and the return value is also relevant. */
    case F_DUPFD:
    case F_DUPFD_CLOEXEC: {
      send_ret = true;
    }
    __attribute__ ((fallthrough));

    /* Commands taking an int arg that the supervisor needs to know about,
     * but the return value is irrelevant (other than not being an error value). */
    case F_SETFD: {
      to_send = true;
      has_int_arg = true;
      /* Start another vararg business that doesn't conflict with the one in call_orig, see #178. */
      va_list ap_int;
###   if not syscall
      /* Find 'arg' of an fcntl(fd, cmd, arg) */
      va_start(ap_int, cmd);
###   else
      /* Find 'arg' of a syscall(SYS_fcntl, fd, cmd, arg) */
      va_start(ap_int, number);
      va_arg(ap_int, int);  /* skip over fd */
      va_arg(ap_int, int);  /* skip over cmd */
###   endif
      int_arg = va_arg(ap_int, int);
      va_end(ap_int);
      break;
    }
#ifdef F_GETPATH
    case F_GETPATH: {
      to_send = true;
      has_string_arg = true;
      /* Start another vararg business that doesn't conflict with the one in call_orig, see #178. */
      va_list ap_string;
###   if not syscall
      /* Find 'arg' of an fcntl(fd, cmd, arg) */
      va_start(ap_string, cmd);
###   else
      /* Find 'arg' of a syscall(SYS_fcntl, fd, cmd, arg) */
      va_start(ap_string, number);
      va_arg(ap_string, int);  /* skip over fd */
      va_arg(ap_string, int);  /* skip over cmd */
###   endif
      string_arg = va_arg(ap_string, char*);
      va_end(ap_string);
      break;
    }
#endif
    /* Commands that don't take an arg (or the arg doesn't matter to
     * the supervisor), but the supervisor needs to know about. This
     * includes all the unrecognized commands. Let's spell out the
     * recognized ones, rather than just catching them by "default",
     * for better readability. */
    default: {
      to_send = true;
      break;
    }
  }
### endblock before

### block after
  switch (cmd) {
    case F_DUPFD:
    case F_DUPFD_CLOEXEC:
      if (i_am_intercepting && success) copy_notify_on_read_write_state(ret, fd);
      break;
    default:
      break;
  }
### endblock after

### block call_orig
  /* Treating the optional parameter as 'void *' should work, see #178. */
  void *voidp_arg = va_arg(ap, void *);
  ret = {{ call_ic_orig_func }}(fd, cmd, voidp_arg);
### endblock call_orig
