{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the popen() call.                                     #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block before
  /*
   * The popen() call interception loops the output of the popen()-ed command through the supervisor
   * using a fifo. The original fd backing the FILE* stream returned by the popen() call is replaced
   * with a fifo endpoint which will be closed by the pclose() call eventually.
   */

  int type_flags = popen_type_to_flags(type);
  {
    pthread_mutex_lock(&ic_system_popen_lock);
    /* Notify the supervisor before the call */
    FBB_Builder_popen ic_msg;
    fbb_popen_init(&ic_msg);
    fbb_popen_set_cmd(&ic_msg, cmd);
    fbb_popen_set_type_flags(&ic_msg, type_flags);
    fb_fbb_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
  }
### endblock before

### block call_orig
  /* Fix up the environment */
  /* This is racy because it operates on the global "environ", but is probably good enough. */
  /* A proper solution would require to prefix "cmd" with a wrapper that fixes it up, but that could be slow. */
  bool do_env_fixup = false;
  char **environ_saved = environ;
  if (env_needs_fixup(environ)) {
    do_env_fixup = true;
    int env_fixup_size = get_env_fixup_size(environ);
    environ = alloca(env_fixup_size);
    env_fixup(environ_saved, environ);
  }

  {{ super() }}

  if (do_env_fixup) {
    environ = environ_saved;
  }
### endblock call_orig

### block send_msg
  {
    /* Notify the supervisor after the call */
    if (success) {
      int ret_fileno = ic_orig_fileno(ret);
      char fd_fifo[fb_conn_string_len + 64];
      FBB_Builder_popen_parent ic_msg;
      fbb_popen_parent_init(&ic_msg);
      if ((type_flags & O_ACCMODE) == O_WRONLY) {
        /* The returned fd is connected to the child's stdin, no pipe is needed. */
        fbb_popen_parent_set_fd(&ic_msg, ret_fileno);
        fb_fbb_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
      } else {
        struct timespec time;
        /* If the returned fd is connected to the child's stdout, capture it to possibly shortcut child. */
        ic_orig_clock_gettime(CLOCK_REALTIME, &time);
        snprintf(fd_fifo, sizeof(fd_fifo), "%s-%d-0-%09ld-%09ld",
                 fb_conn_string, getpid(), time.tv_sec, time.tv_nsec);
        int fifo_ret = ic_orig_mkfifo(fd_fifo, 0666);
        if (fifo_ret == -1) {
          // TODO(rbalint) maybe continue without shortcutting being possible
          assert(ret == 0 && "mkfifo for popen() failed");
        }
        /* Send fifo to supervisor to open it first there because opening in blocking
           mode blocks until the other end is opened, too (see fifo(7)) */
        int tmp_fifo_fd = ic_orig_open(fd_fifo, type_flags | O_NONBLOCK);
        assert(tmp_fifo_fd != -1);
        fbb_popen_parent_set_fd(&ic_msg, ret_fileno);
        fbb_popen_parent_set_fifo(&ic_msg, fd_fifo);
        fb_fbb_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
        ic_orig_fcntl(tmp_fifo_fd, F_SETFL, ic_orig_fcntl(ret_fileno, F_GETFL));
#ifndef NDEBUG
        int dup2_ret =
#endif
            ic_orig_dup2(tmp_fifo_fd, ret_fileno);
        assert(dup2_ret == ret_fileno);
        /* The pipe will be kept open by ret_fileno, tmp_fifo_fd is not needed anymore. */
        ic_orig_close(tmp_fifo_fd);
      }
    } else {
      FBB_Builder_popen_failed ic_msg;
      fbb_popen_failed_init(&ic_msg);
      fbb_popen_failed_set_error_no(&ic_msg, saved_errno);
      fb_fbb_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
    }
    pthread_mutex_unlock(&ic_system_popen_lock);
  }
### endblock send_msg
