{# ------------------------------------------------------------------ #}
{# Copyright (c) 2022 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the recv[m]msg() calls.                               #}
{# In addition to the "read" template, i.e. notifying the supervisor  #}
{# on a read from an inherited fd, we also need to notify the         #}
{# supervisor if a file descriptor was received using SCM_RIGHTS.     #}
{# This is done in another FBB message.                               #}
{# ------------------------------------------------------------------ #}
### extends "tpl_read.c"

### set is_pread = "false"

{% set msg = "read_from_inherited" %}
{# No locking around the recv[m]msg(): see issue #279 #}
{% set global_lock = 'never' %}

### block send_msg
  {{ super() }}  {# tpl_read.c's stuff #}

{% if func == 'recvmmsg' %}
  /* recvmmsg() can return multiple messages. Loop over them. For simplicity, handle them
   * separately, i.e. send separate FBB messages if more of them have SCM_RIGHTS fds. */
  int i;
  for (i = 0; i < ret; i++) {
    struct msghdr *msg = &msgvec[i].msg_hdr;
{% endif %}

    /* A message can have multiple ancillary messages attached to it, each of them can carry multiple
     * file descriptors. It's possible, even though unlikely, that multiple ancillary messages contain
     * some fds. Especially since the Linux kernel seems to flatten them out into a single ancillary
     * message of the type SCM_RIGHTS.
     *
     * To simplify our lives, send a separate FBB message to the supervisor for each such ancillary
     * message of type SCM_RIGHTS. However, for each ancillary message, send all its fds in a single
     * FBB message as an array. */
    struct cmsghdr *cmsg;
    for (cmsg = CMSG_FIRSTHDR(msg); cmsg != NULL; cmsg = CMSG_NXTHDR(msg, cmsg)) {
      if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
        /* How do we figure out how many fds we actually received? It's unclear to me.
         * My best bet is that we'd need the inverse of CMSG_LEN(), for which there is no macro.
         * We could find the inverse by calling CMSG_LEN() with our guesses in a loop. Or open
         * up the definition from the glibc header to invert it, which is what I'm doing here. */
        int len = cmsg->cmsg_len - CMSG_ALIGN(sizeof (struct cmsghdr));
        assert(len >= 0);
        int num_fds = len / sizeof(int);
        if (num_fds > 0) {
          /* Notify the supervisor */
          FBBCOMM_Builder_recvmsg_scm_rights ic_msg1;
          fbbcomm_builder_recvmsg_scm_rights_init(&ic_msg1);
          fbbcomm_builder_recvmsg_scm_rights_set_cloexec(&ic_msg1, flags & MSG_CMSG_CLOEXEC);
#pragma GCC diagnostic push
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wcast-align"
#endif
          fbbcomm_builder_recvmsg_scm_rights_set_fds(&ic_msg1, (int *) CMSG_DATA(cmsg), num_fds);
#pragma GCC diagnostic pop
          {{ grab_lock_if_needed('true') | indent(8) }}
          fb_fbbcomm_send_msg(&ic_msg1, fb_sv_conn);
          {{ release_lock_if_needed() | indent(8) }}
        }
      }
    }

{% if func == 'recvmmsg' %}
  }  /* looping over msgvec */
{% endif %}

### endblock send_msg
