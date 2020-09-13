{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template to generate fbb.h.                                        #}
{# ------------------------------------------------------------------ #}

/* Auto-generated by generate_fbb, do not edit */

#define FBB_DEBUG 1

#ifndef FBB_H
#define FBB_H 1

#ifdef __cplusplus
#include <string>
#include <vector>
#endif

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

#include "common/firebuild_common.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline size_t strarraysize(char * const *p) {
  if (p == NULL) return 0;
  size_t s = 0;
  while (*p != NULL) {
    s += strlen(*p) + 1;
    p++;
  }
  return s;
}

enum {
  /* Values are spelled out for easier debugging */
### for (msg, _) in msgs
  FBB_TAG_{{ msg }} = {{ loop.index0 }},
### endfor
  FBB_TAG_NEXT
};

### for (msg, fields) in msgs
/************************ {{ msg }} ************************/

/* wire buffer */
typedef struct _FBB_{{ msg }} {
  /* it's important that the tag is the very first field */
  int fbb_tag;
  /* scalar fields */
###   for (req, type, var) in fields
###     if type not in [STRING, STRINGARRAY]
  {{ type }} {{ var }};
###     endif
###   endfor
  /* sizes of string and stringarray fields */
###   for (req, type, var) in fields
###     if type in [STRING, STRINGARRAY]
  size_t {{ var }}_size;
###     endif
###   endfor
  /* whether optional scalars have been set */
###   for (req, type, var) in fields
###     if type not in [STRING, STRINGARRAY] and req == OPTIONAL
  bool has_{{ var }} : 1;
###     endif
###   endfor
} FBB_{{ msg }};

/* builder */
typedef struct _FBB_Builder_{{ msg }} {
  /* the message, except for the strings and stringarrays */
  FBB_{{ msg }} wire;
  /* the strings and stringarrays (pointers only, owned by the caller) */
###   for (req, type, var) in fields
###     if type == STRING
  const char *{{ var }};
###     elif type == STRINGARRAY
  char * const *{{ var }};
###     endif
###   endfor
#if FBB_DEBUG
  /* whether required scalars have been set */
###   for (req, type, var) in fields
###     if type not in [STRING, STRINGARRAY] and req == REQUIRED
  bool has_{{ var }} : 1;
###     endif
###   endfor
#endif
} FBB_Builder_{{ msg }};

/* init, set tag */
static inline void fbb_{{ msg }}_init(FBB_Builder_{{ msg }} *msgbldr) {
  memset(msgbldr, 0, sizeof(*msgbldr));
  msgbldr->wire.fbb_tag = FBB_TAG_{{ msg }};
}

###   for (req, type, var) in fields
###     if type == STRING
/* set string '{{ var }}' */
static inline void fbb_{{ msg }}_set_{{ var }}(FBB_Builder_{{ msg }} *msgbldr, const char *value) {
#if FBB_DEBUG
  assert(msgbldr->wire.fbb_tag == FBB_TAG_{{ msg }});
#endif

  msgbldr->wire.{{ var }}_size = value == NULL ? 0 : strlen(value) + 1;
  msgbldr->{{ var }} = value;
}

###     elif type == STRINGARRAY
/* set stringarray '{{ var }}' */
static inline void fbb_{{ msg }}_set_{{ var }}(FBB_Builder_{{ msg }} *msgbldr, char * const *value) {
#if FBB_DEBUG
  assert(msgbldr->wire.fbb_tag == FBB_TAG_{{ msg }});
#endif

  msgbldr->wire.{{ var }}_size = value == NULL ? 0 : strarraysize(value);
  msgbldr->{{ var }} = value;
}

###     else
/* set {{ req }} scalar '{{ var }}' */
static inline void fbb_{{ msg }}_set_{{ var }}(FBB_Builder_{{ msg }} *msgbldr, {{ type }} value) {
#if FBB_DEBUG
  assert(msgbldr->wire.fbb_tag == FBB_TAG_{{ msg }});
#endif

  msgbldr->wire.{{ var }} = value;
###       if req == OPTIONAL
  msgbldr->wire.has_{{ var }} = true;
###       elif req == REQUIRED
#if FBB_DEBUG
  msgbldr->has_{{ var }} = true;
#endif
###       endif
}

###     endif
###   endfor

###   set ns = namespace(offset_str="")
###   for (req, type, var) in fields
###     if req == OPTIONAL
/* whether optional {{ type if type in [STRING, STRINGARRAY] else "scalar" }} '{{ var }}' is present */
static inline bool fbb_{{ msg }}_has_{{ var }}(const FBB_{{ msg }} *msg) {
#if FBB_DEBUG
  assert(msg->fbb_tag == FBB_TAG_{{ msg }});
#endif

###       if type in [STRING, STRINGARRAY]
  return msg->{{ var }}_size > 0;
###       else
  return msg->has_{{ var }};
###       endif
}

###     endif

###     if type == STRING
/* get string '{{ var }}', assuming the wire format in memory, i.e. the struct is followed by the raw strings */
static inline const char *fbb_{{ msg }}_get_{{ var }}(const FBB_{{ msg }} *msg) {
#if FBB_DEBUG
  assert(msg->fbb_tag == FBB_TAG_{{ msg }});
#endif

###       if req == OPTIONAL
#if FBB_DEBUG
  assert(msg->{{ var }}_size > 0);
#endif
###       endif
  return (const char *)(msg) + sizeof(*msg){{ ns.offset_str }};
###     set ns.offset_str = ns.offset_str + " + msg->" + var + "_size"
}

###     elif type == STRINGARRAY
#define for_s_in_fbb_{{ msg }}_{{ var }}(msg, loop_body) do {            \
  size_t rem_size = msg->{{ var }}_size;                                 \
  const char *s = (const char *)(msg) + sizeof(*msg){{ ns.offset_str }}; \
  while (rem_size > 0) {                                                 \
    loop_body                                                            \
    size_t size = strlen(s) + 1;                                         \
    rem_size -= size;                                                    \
    s += size;                                                           \
  }                                                                      \
} while (0)

#ifdef __cplusplus
/* get stringarray '{{ var }}', assuming the wire format in memory, i.e. the struct is followed by the raw strings */
static inline std::vector<std::string> fbb_{{ msg }}_get_{{ var }}(const FBB_{{ msg }} *msg) {
#if FBB_DEBUG
  assert(msg->fbb_tag == FBB_TAG_{{ msg }});
#endif

  std::vector<std::string> ret;
  size_t rem_size = msg->{{ var }}_size;
  const char *strs = (const char *)(msg) + sizeof(*msg){{ ns.offset_str }};
###       set ns.offset_str = ns.offset_str + " + msg->" + var + "_size"
  while (rem_size > 0) {
    ret.push_back(strs);
    size_t size = strlen(strs) + 1;
    rem_size -= size;
    strs += size;
  }
  return ret;
}
#endif

###     else
/* get {{ req }} scalar '{{ var }}' */
static inline {{ type }} fbb_{{ msg }}_get_{{ var }}(const FBB_{{ msg }} *msg) {
#if FBB_DEBUG
  assert(msg->fbb_tag == FBB_TAG_{{ msg }});
#endif

###       if req == OPTIONAL
#if FBB_DEBUG
  assert(msg->has_{{ var }});
#endif
###       endif
  return msg->{{ var }};
}

###       if req == OPTIONAL
/* get {{ req }} scalar '{{ var }}' with fallback default */
static inline {{ type }} fbb_{{ msg }}_get_{{ var }}_with_fallback(const FBB_{{ msg }} *msg, {{ type }} fallback) {
  return fbb_{{ msg }}_has_{{ var }}(msg) ? fbb_{{ msg }}_get_{{ var }}(msg) : fallback;
}

###       endif
###     endif
###   endfor

### endfor

/************************************************/

/* debug any message */
void fbb_debug(const void *msg);

/* send any message */
void fbb_send(int fd, const void *msgbldr, uint32_t ack_id);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* FBB_H */
