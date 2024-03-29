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
{# Template for the exec() family.                                    #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

{# Nasty hacks. Note that the func[4:] stuff also work with "fexecve". #}
{% set l = ('l' in func[4:]) %}
{% set v = ('v' in func[4:]) %} {# Exactly one of 'l' and 'v' is True. #}
{% set p = ('p' in func[4:]) %}
{% set e = ('e' in func[4:]) %}
{% set f = (func[0] == 'f') %}
{% set at = (func[-2:] == 'at') %}

### block body
###   if l
  /* Convert "arg, ..." to "argv[]" on the stack (async-signal-safe) */
  unsigned int argc = 1;
  unsigned int i;
  while (va_arg(ap, char*) != NULL) {
    argc++;
  }
  va_end(ap);
  char *argv[argc + 1];
  argv[0] = (/* non-const */ char *) arg;
  va_start(ap, {{ args[-1]['name'] }});
  for (i = 1; i <= argc ; i++) {
    argv[i] = va_arg(ap, char*);
  }

###     if e
  /* Also locate the environment */
  char **envp = va_arg(ap, char**);
###     endif
###   endif
###   if not e
  /* Use the global environment */
  char **envp = environ;
###   endif

  /* Fix up the environment */
  void *env_fixed_up;
  if (i_am_intercepting && env_needs_fixup((char **) envp)) {
    int env_fixup_size = get_env_fixup_size((char **) envp);
    env_fixed_up = alloca(env_fixup_size);
    env_fixup((char **) envp, env_fixed_up);
  } else {
    env_fixed_up = (void *) envp;
  }

  if (i_am_intercepting) {
    /* Notify the supervisor before the call */
    FBBCOMM_Builder_exec ic_msg;
    fbbcomm_builder_exec_init(&ic_msg);
###   if not f
    fbbcomm_builder_exec_set_file(&ic_msg, file);
###   else
    /* Set for fexec*() */
    fbbcomm_builder_exec_set_fd(&ic_msg, fd);
###   endif
###   if at
    /* Set for exec*at() */
    fbbcomm_builder_exec_set_dirfd(&ic_msg, dirfd);
    // TODO(rbalint) see #32 fbbcomm_builder_exec_set_flags(&ic_msg, flags);
###   endif
###   if p
    /* Set for exec*p*() */
    fbbcomm_builder_exec_set_with_p(&ic_msg, true);
    char *path_env;
    size_t confstr_buf_len = 0;
    if ((path_env = getenv("PATH"))) {
      fbbcomm_builder_exec_set_path(&ic_msg, path_env);
    } else {
      /* We have to fall back as described in man execvp.
       * This code is for glibc >= 2.24. For older versions
       * we'd need to prepend ".:", see issue 153. */
      confstr_buf_len = get_ic_orig_confstr()(_CS_PATH, NULL, 0);
    }
    /* Use the stack rather than the heap, make sure it lives
     * until we send the message. */
    if (confstr_buf_len > 0) {
      char *path_confstr = alloca(confstr_buf_len);
      get_ic_orig_confstr()(_CS_PATH, path_confstr, confstr_buf_len);
      fbbcomm_builder_exec_set_path(&ic_msg, path_confstr);
    }
###   endif

    /* Command line arguments */
    fbbcomm_builder_exec_set_arg(&ic_msg, (const char **) argv);

    /* Environment variables */
    fbbcomm_builder_exec_set_env(&ic_msg, (const char **) env_fixed_up);

    /* Get CPU time used up to this exec() */
    struct rusage ru;
    rusage_since_exec(&ru);
    reset_rusage();
    fbbcomm_builder_exec_set_utime_u(&ic_msg,
        (int64_t)ru.ru_utime.tv_sec * 1000000 + (int64_t)ru.ru_utime.tv_usec);
    fbbcomm_builder_exec_set_stime_u(&ic_msg,
        (int64_t)ru.ru_stime.tv_sec * 1000000 + (int64_t)ru.ru_stime.tv_usec);

    fb_fbbcomm_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
  }

  /* Perform the call. */
{% set ic_orig_func = "ic_orig_" + func %}
###   if l
  /* Instead of execl*(), call its execv*() counterpart. */
{% set ic_orig_func = ic_orig_func.replace("l", "v") %}
###   endif
###   if not e and not (target == "darwin" and ic_orig_func == "ic_orig_execvp")
  /* Instead of exec*() without "e", call its exec*e() counterpart. */
{% set ic_orig_func = ic_orig_func + "e" %}
###   endif
###   if syscall
{% set call_ic_orig_func = ic_orig_func %}
### else
{% set call_ic_orig_func = "get_" + ic_orig_func + "()" %}
###   endif
  errno = saved_errno;
###   if ic_orig_func == "ic_orig_execvp"
  char **env_saved = environ;
  environ = env_fixed_up;
###   endif
  ret = {{ call_ic_orig_func }}({% if at %}dirfd, {% endif %}{% if f %}fd{% else %}file{% endif %}, argv{% if not ic_orig_func == "ic_orig_execvp" %}, env_fixed_up{% endif %}{% if at %}, flags{% endif %});
###   if ic_orig_func == "ic_orig_execvp"
 environ = env_saved;
###   endif
  saved_errno = errno;

  if (i_am_intercepting) {
    /* Notify the supervisor after the call */
    FBBCOMM_Builder_exec_failed ic_msg;
    fbbcomm_builder_exec_failed_init(&ic_msg);
    fbbcomm_builder_exec_failed_set_error_no(&ic_msg, saved_errno);
    /* It's important to wait for ACK, so that if this process now exits and its parent
     * successfully waits for it then the supervisor won't incorrectly see it in
     * exec_pending state and won't incorrectly believe that a statically linked binary
     * was execed. See #324 for details. */
    fb_fbbcomm_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
  }
### endblock body
