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
{# Template for the pipe() and pipe2() calls.                         #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block call_orig
  if (i_am_intercepting) {
    /* No signal between sending the "pipe_request" message and receiving its "pipe_fds" response. */
    thread_signal_danger_zone_enter();

    /* Step 1/3. See #656 for design rationale.
     * Request the supervisor to create an intercepted unnamed pipe for us. */
    FBBCOMM_Builder_pipe_request ic_msg1;
    fbbcomm_builder_pipe_request_init(&ic_msg1);
    fbbcomm_builder_pipe_request_set_flags(&ic_msg1, flags);
    fb_fbbcomm_send_msg(&ic_msg1, fb_sv_conn);

    /* Step 2/3. Receive the response from the supervisor, which carries
     * the file descriptors as ancillary data (SCM_RIGHTS).
     * The real data we're expecting to arrive is the usual message header
     * followed by a serialized FBB "pipe_created" message. */
    msg_header sv_msg_hdr;
    uint64_t sv_msg_buf[8];  /* Should be large enough for the serialized "pipe_created" message. */

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
      char buf[CMSG_SPACE(2 * sizeof(int))];
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

    /* Read the payload, with possibly two attached fds as ancillary data.
     *
     * The supervisor places this in the socket as an atomic step when the queue is almost empty, so
     * we don't expect a short read. However, a signal interrupt might occur.
     *
     * Set the O_CLOEXEC bit to the desired value.
     * The fcntl(..., F_SETFL, ...) bits were set by the supervisor. */
###   if target == "darwin"
/* MSG_CMSG_CLOEXEC is not defined on OS X, but it would not be used anyway because pipe2 is
 * missing, too. */
#define MSG_CMSG_CLOEXEC 0
###   endif
#ifndef NDEBUG
    received =
#endif
        TEMP_FAILURE_RETRY(get_ic_orig_recvmsg()(fb_sv_conn, &msgh, (flags & O_CLOEXEC) ? MSG_CMSG_CLOEXEC : 0));
    assert(received >= 0 && received == (ssize_t)sv_msg_hdr.msg_size);
    assert(fbbcomm_serialized_get_tag((FBBCOMM_Serialized *) sv_msg_buf) == FBBCOMM_TAG_pipe_created);

    thread_signal_danger_zone_leave();

    FBBCOMM_Serialized_pipe_created *sv_msg = (FBBCOMM_Serialized_pipe_created *) sv_msg_buf;
    if (fbbcomm_serialized_pipe_created_has_error_no(sv_msg)) {
      /* Supervisor reported an error. */
      assert(sv_msg_hdr.fd_count == 0);
      errno = fbbcomm_serialized_pipe_created_get_error_no(sv_msg);
      ret = -1;
    } else {
      /* The supervisor successfully created the pipes. See their local fds. */
      assert(sv_msg_hdr.fd_count == 2);
      struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msgh);
      if (!cmsg || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS || cmsg->cmsg_len != CMSG_LEN(2 * sizeof(int))) {
        /* fds missing, maybe file limit in this process exceeded? */
        errno = EMFILE;
        ret = -1;
      } else {
        /* Two fds found as expected. */
        memcpy(pipefd, CMSG_DATA(cmsg), 2 * sizeof(int));
        ret = 0;
      }
    }
  } else {
    /* just create the pipe */
###   if target == "darwin"
    ret = get_ic_orig_pipe()(pipefd);
###   else
    ret = get_ic_orig_pipe2()(pipefd, flags);
###   endif
  }
### endblock call_orig

{% set send_msg_on_error = False %}
{% set msg = "pipe_fds" %}
{% set msg_skip_fields = ["pipefd", "flags"] %}
{% set msg_add_fields = ["if (success) {",
                         "  fbbcomm_builder_pipe_fds_set_fd0(&ic_msg, pipefd[0]);",
                         "  fbbcomm_builder_pipe_fds_set_fd1(&ic_msg, pipefd[1]);",
                         "}"] %}
### block send_msg
  /* Step 3/3. Send the interceptor-side fd numbers to the supervisor. */
  {{ super() }}
### endblock send_msg
