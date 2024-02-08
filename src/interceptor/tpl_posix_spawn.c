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
{# Template for the posix_spawn() family.                             #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block before
{# attr_flags could be forwarded to the supervisor on Linux, too, but #}
{# they would not be used for anything by Firebuild, only on macOS.   #}
###   if target == "darwin"
  short attr_flags = 0;
###   endif
  /* Fix up the environment */
  void *env_fixed_up;
  if (i_am_intercepting && env_needs_fixup((char **) envp)) {
    int env_fixup_size = get_env_fixup_size((char **) envp);
    env_fixed_up = alloca(env_fixup_size);
    env_fixup((char **) envp, env_fixed_up);
  } else {
    env_fixed_up = (char **)envp;
  }
  if (i_am_intercepting) {
    pthread_mutex_lock(&ic_system_popen_lock);
    /* Notify the supervisor before the call */
    FBBCOMM_Builder_posix_spawn ic_msg;
    fbbcomm_builder_posix_spawn_init(&ic_msg);
    fbbcomm_builder_posix_spawn_set_file(&ic_msg, file);
    if (file_actions) {
      voidp_array *p = psfa_find(file_actions);
      assert(p);
      fbbcomm_builder_posix_spawn_set_file_actions(&ic_msg, (const FBBCOMM_Builder **) (p->p));
    }
###   if func == 'posix_spawnp'
    fbbcomm_builder_posix_spawn_set_is_spawnp(&ic_msg, true);
###   else
    fbbcomm_builder_posix_spawn_set_is_spawnp(&ic_msg, false);
###   endif
    fbbcomm_builder_posix_spawn_set_arg(&ic_msg, (const char **) argv);
    fbbcomm_builder_posix_spawn_set_env(&ic_msg, (const char **) env_fixed_up);
###   if target == "darwin"
    if (attrp) {
      if (posix_spawnattr_getflags(attrp, &attr_flags) == 0) {
        fbbcomm_builder_posix_spawn_set_attr_flags(&ic_msg, attr_flags);
        if (attr_flags & POSIX_SPAWN_SETEXEC) {
          struct rusage ru;
          rusage_since_exec(&ru);
          fbbcomm_builder_posix_spawn_set_utime_u(
              &ic_msg, (int64_t)ru.ru_utime.tv_sec * 1000000 + (int64_t)ru.ru_utime.tv_usec);
          fbbcomm_builder_posix_spawn_set_stime_u(
              &ic_msg, (int64_t)ru.ru_stime.tv_sec * 1000000 + (int64_t)ru.ru_stime.tv_usec);
        }
      }
    }
###   endif
    fb_fbbcomm_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
  }
### endblock before

### block call_orig
###   if func not in ['pidfd_spawn', 'pidfd_spawnp']
  /* Fix up missing out parameter for internal use */
  pid_t tmp_pid;
  if (!pid) {
    pid = &tmp_pid;
  }
###   endif
  ret = get_ic_orig_{{ func }}()({{ names_str | replace("envp", "env_fixed_up")}});
### endblock call_orig

### block send_msg
  if (i_am_intercepting) {
    /* Notify the supervisor after the call */
    if (success) {
      FBBCOMM_Builder_posix_spawn_parent ic_msg;
      fbbcomm_builder_posix_spawn_parent_init(&ic_msg);
      fbbcomm_builder_posix_spawn_parent_set_arg(&ic_msg, (const char **) argv);
      if (file_actions) {
        voidp_array *p = psfa_find(file_actions);
        assert(p);
        fbbcomm_builder_posix_spawn_parent_set_file_actions(&ic_msg, (const FBBCOMM_Builder **) (p->p));
      }
###   if func in ['pidfd_spawn', 'pidfd_spawnp']
      fbbcomm_builder_posix_spawn_parent_set_pid(&ic_msg, pidfd_getpid(*pid));
###   else
      fbbcomm_builder_posix_spawn_parent_set_pid(&ic_msg, *pid);
###   endif
###   if target == "darwin"
      if (attr_flags != 0) {
        fbbcomm_builder_posix_spawn_parent_set_attr_flags(&ic_msg, attr_flags);
      }
###   endif
      fb_fbbcomm_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
    } else {
      /* Unlike at most other methods where we skip on EINTR or EFAULT, here we always have to send
       * a counterpart to the posix_spawn message. */
      FBBCOMM_Builder_posix_spawn_failed ic_msg;
      fbbcomm_builder_posix_spawn_failed_init(&ic_msg);
      fbbcomm_builder_posix_spawn_failed_set_arg(&ic_msg, (const char **) argv);
      /* errno is not documented to be set, the error code is in the return value. */
      fbbcomm_builder_posix_spawn_failed_set_error_no(&ic_msg, ret);
      fb_fbbcomm_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
    }
    pthread_mutex_unlock(&ic_system_popen_lock);
  }
### endblock send_msg
