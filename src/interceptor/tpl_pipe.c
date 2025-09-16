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
    if (flags != 0) {
      fbbcomm_builder_pipe_request_set_flags(&ic_msg1, flags);
    }
    fb_fbbcomm_send_msg(&ic_msg1, fb_sv_conn);

    /* Step 2/3. Receive the response from the supervisor, which carries
     * the file descriptors as ancillary data (SCM_RIGHTS).
     * The real data we're expecting to arrive is the usual message header
     * followed by a serialized FBB "pipe_created" message. */
    FBBCOMM_READ_MSG_HEADER_AND_ALLOC_BODY(fb_sv_conn, sv_msg_hdr, sv_msg_buf);
    FBBCOMM_CREATE_RECVMSG_HEADER(msgh, sv_msg_hdr, sv_msg_buf, 2);

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
    FBBCOMM_RECVMSG(pipe_created, sv_msg, sv_msg_buf, fb_sv_conn, msgh,
                    (flags & O_CLOEXEC) ? MSG_CMSG_CLOEXEC : 0);
    thread_signal_danger_zone_leave();

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
