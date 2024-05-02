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
  if (i_am_intercepting) {
    pthread_mutex_lock(&ic_system_popen_lock);
    /* Notify the supervisor before the call */
    FBBCOMM_Builder_popen ic_msg;
    fbbcomm_builder_popen_init(&ic_msg);
    fbbcomm_builder_popen_set_cmd(&ic_msg, cmd);
    fbbcomm_builder_popen_set_type_flags(&ic_msg, type_flags);
    fb_fbbcomm_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
  }
### endblock before

### block call_orig
  ENVIRON_SAVE_AND_FIXUP(did_env_fixup, environ_saved);

  {{ super() }}

  ENVIRON_RESTORE(did_env_fixup, environ_saved);
### endblock call_orig

### block after
  if (success) {
    assert(!voidp_set_contains(&popened_streams, ret));
    voidp_set_insert(&popened_streams, ret);
  }
### endblock

### block send_msg
  if (i_am_intercepting) {
    /* Notify the supervisor after the call */
    if (success) {
      /* No signal between sending the "popen_parent" message and receiving its "popen_fd" response. */
      thread_signal_danger_zone_enter();

      int ret_fileno = get_ic_orig_fileno()(ret);
      FBBCOMM_Builder_popen_parent ic_msg;
      fbbcomm_builder_popen_parent_init(&ic_msg);
      fbbcomm_builder_popen_parent_set_fd(&ic_msg, ret_fileno);
      fb_fbbcomm_send_msg(&ic_msg, fb_sv_conn);

      /* Receive the response from the supervisor, which carries
       * the file descriptor as ancillary data (SCM_RIGHTS).
       * The real data we're expecting to arrive is the usual message header
       * followed by a serialized FBB "popen_fd" message. */
      msg_header sv_msg_hdr;
      uint64_t sv_msg_buf[8];  /* Should be large enough for the serialized "popen_fd" message. */

      /* Read the header. */
#ifndef NDEBUG
      ssize_t received =
#endif
          fb_read(fb_sv_conn, &sv_msg_hdr, sizeof(sv_msg_hdr));
      assert(received == sizeof(sv_msg_hdr));
      assert(sv_msg_hdr.ack_id == 0);  // FIXME maybe send a real ack_id

      /* Taken from cmsg(3). */
      union {  /* Ancillary data buffer, wrapped in a union
                  in order to ensure it is suitably aligned */
        char buf[CMSG_SPACE(1 * sizeof(int))];
        struct cmsghdr align;
      } u = { 0 };

      struct iovec iov = { 0 };
      iov.iov_base = sv_msg_buf;
      iov.iov_len = sv_msg_hdr.msg_size;

      struct msghdr msgh = { 0 };
      msgh.msg_iov = &iov;
      msgh.msg_iovlen = 1;
      msgh.msg_control = u.buf;
      msgh.msg_controllen = sizeof(u.buf);

      /* Read the payload, with the attached fd as ancillary data.
       *
       * The supervisor places this in the socket as an atomic step when the queue is almost empty,
       * so we don't expect a short read. However, a signal interrupt might occur. */
#ifndef NDEBUG
      received =
#endif
          TEMP_FAILURE_RETRY(
#if defined(_TIME_BITS) && (_TIME_BITS == 64)
              get_ic_orig___recvmsg64()(
#else
              get_ic_orig_recvmsg()(
#endif
                  fb_sv_conn, &msgh, 0));
      assert(received >= 0 && received == (ssize_t)sv_msg_hdr.msg_size);
      assert(fbbcomm_serialized_get_tag((FBBCOMM_Serialized *) sv_msg_buf) == FBBCOMM_TAG_popen_fd);
      assert(sv_msg_hdr.fd_count == 1);

      thread_signal_danger_zone_leave();

      struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msgh);
      if (!cmsg || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS || cmsg->cmsg_len != CMSG_LEN(1 * sizeof(int))) {
        assert(0 && "expected ancillary fd missing");
      } else {
        /* fd found as expected. */
        int ancillary_fd;
        memcpy(&ancillary_fd, CMSG_DATA(cmsg), sizeof(int));
        /* Move to the desired slot. Set the O_CLOEXEC bit to the desired value.
         * The fcntl(..., F_SETFL, ...) bits were set by the supervisor. */
        assert(ancillary_fd != ret_fileno);  /* because ret_fileno is still open */
###   if target == "linux"
        if (TEMP_FAILURE_RETRY(get_ic_orig_dup3()(ancillary_fd, ret_fileno, type_flags & O_CLOEXEC))
            != ret_fileno) {
          assert(0 && "dup3() on the popened fd failed");
        }
###   else
        if (TEMP_FAILURE_RETRY(get_ic_orig_dup2()(ancillary_fd, ret_fileno))
            != ret_fileno) {
          assert(0 && "dup2() on the popened fd failed");
        }
        if (TEMP_FAILURE_RETRY(
#if defined(_TIME_BITS) && (_TIME_BITS == 64)
                get_ic_orig___fcntl_time64()(
#else
                get_ic_orig_fcntl()(
#endif
                    ret_fileno, F_SETFD, type_flags & FD_CLOEXEC)) != 0) {
          assert(0 && "fcntl() on the popened fd failed");
        }
###   endif
        /* POSIX says to retry close() on EINTR (e.g. wrap in TEMP_FAILURE_RETRY())
         * but Linux probably disagrees, see #723. */
        if (get_ic_orig_close()(ancillary_fd) < 0) {
          assert(0 && "close() on the dup3()d popened fd failed");
        }
      }
    } else {
      FBBCOMM_Builder_popen_failed ic_msg;
      fbbcomm_builder_popen_failed_init(&ic_msg);
      fbbcomm_builder_popen_failed_set_error_no(&ic_msg, saved_errno);
      fb_fbbcomm_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
    }
    pthread_mutex_unlock(&ic_system_popen_lock);
  }
### endblock send_msg
