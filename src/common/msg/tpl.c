{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template to generate fbb.c.                                        #}
{# ------------------------------------------------------------------ #}

/* Auto-generated by generate_fbb, do not edit */

#include "./fbb.h"

#ifdef __cplusplus
extern "C" {
#endif

### for (msg, fields) in msgs
/************************ {{ msg }} ************************/

/* debug a received '{{ msg }}' message */
static void fbb_{{ msg }}_debug(const void *msg_void) {
  const FBB_{{ msg }} *msg = (const FBB_{{ msg }} *) msg_void;

  fprintf(stderr, "{{ msg }} {\n");
###   for (req, type, var) in fields
###     if req == OPTIONAL
  if (fbb_{{ msg }}_has_{{ var }}(msg)) {
###     else
  if (1) {
###     endif
###     if type == STRING
    fprintf(stderr, "  {{ var }}: \"%s\"\n", fbb_{{ msg }}_get_{{ var }}(msg));
###     elif type == STRINGARRAY
    for_s_in_fbb_{{ msg }}_{{ var }}(msg, {
      fprintf(stderr, "  {{ var }}: \"%s\"\n", s);
    });
###     else
    fprintf(stderr, "  {{ var }}: %lld\n", (long long) fbb_{{ msg }}_get_{{ var }}(msg));
###     endif
  }
###   endfor
  fprintf(stderr, "}\n");
}

/* send a '{{ msg }}' message over the wire */
static void fbb_{{ msg }}_send(int fd, const void *msgbldr_void, uint32_t ack_id) {
  const FBB_Builder_{{ msg }} *msgbldr = (const FBB_Builder_{{ msg }} *) msgbldr_void;

  /* verify that required fields were set */
###   for (req, type, var) in fields
###     if req == REQUIRED
###       if type in [STRING, STRINGARRAY]
  assert(msgbldr->{{ var }} != NULL);
###       else
  assert(msgbldr->has_{{ var }});
###       endif
###     endif
###   endfor
  /* construct and send message */
###   set ns = namespace(string_count=0)
###   for (req, type, var) in fields
###     if type == STRING
###       set ns.string_count = ns.string_count + 1
###     endif
###   endfor
  int string_count = {{ ns.string_count }};
###   for (req, type, var) in fields
###     if type == STRINGARRAY
  if (msgbldr->wire.{{ var }}_size > 0) {
    char * const *p = msgbldr->{{ var }};
    while (*p++) string_count++;
  }
###     endif
###   endfor
  struct iovec iov[3 + string_count];
  uint32_t payload_length = sizeof(msgbldr->wire);
  iov[0].iov_base = &payload_length;
  iov[0].iov_len = sizeof(payload_length);
  iov[1].iov_base = &ack_id;
  iov[1].iov_len = sizeof(ack_id);
  iov[2].iov_base = (/* non-const */ void *) &msgbldr->wire;
  iov[2].iov_len = sizeof(msgbldr->wire);
  int iovcnt = 3;
###   for (req, type, var) in fields
###     if type == STRING
  if (msgbldr->wire.{{ var }}_size > 0) {
    iov[iovcnt].iov_base = (/* non-const */ void *) msgbldr->{{ var }};
    iov[iovcnt].iov_len = msgbldr->wire.{{ var }}_size;
    iovcnt++;
    payload_length += msgbldr->wire.{{ var }}_size;
  }
###     elif type == STRINGARRAY
  if (msgbldr->wire.{{ var }}_size > 0) {
    char * const *p = msgbldr->{{ var }};
    while (*p) {
      iov[iovcnt].iov_base = (/* non-const */ void *) (*p);
      iov[iovcnt].iov_len = strlen(*p) + 1;
      iovcnt++;
      p++;
    }
    payload_length += msgbldr->wire.{{ var }}_size;
  }
###     endif
###   endfor
  fb_writev(fd, iov, iovcnt);
}

### endfor

/************************************************/

/* lookup array for the debugger function of a particular message tag */
static void (*fbb_debuggers_array[])(const void *) = {
### for (msg, _) in msgs
  fbb_{{ msg }}_debug,
### endfor
};

/* debug any message */
void fbb_debug(const void *msg) {
  int tag = * ((int *) msg);
  assert(tag >= 0 && tag < FBB_TAG_NEXT);
  (*fbb_debuggers_array[tag])(msg);
}

/* lookup array for the sender function of a particular message tag */
static void (*fbb_senders_array[])(int, const void *, uint32_t) = {
### for (msg, _) in msgs
  fbb_{{ msg }}_send,
### endfor
};

/* send any message */
void fbb_send(int fd, const void *msgbldr, uint32_t ack_id) {
  if (msgbldr != NULL) {
    /* invoke the particular sender for this message type */
    int tag = * ((int *) msgbldr);
    assert(tag >= 0 && tag < FBB_TAG_NEXT);
    (*fbb_senders_array[tag])(fd, msgbldr, ack_id);
  } else {
    /* send an empty message (header with length and ack_id only) */
    struct iovec iov[2];
    uint32_t payload_length = 0;
    iov[0].iov_base = &payload_length;
    iov[0].iov_len = sizeof(payload_length);
    iov[1].iov_base = &ack_id;
    iov[1].iov_len = sizeof(ack_id);
    fb_writev(fd, iov, 2);
  }
}

#ifdef __cplusplus
}  /* extern "C" */
#endif
