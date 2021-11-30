{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020, 2021 Interri Kft.                              #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template to generate {{ ns }}.h.                                   #}
{# ------------------------------------------------------------------ #}

/* Auto-generated by generate_fbb, do not edit */  {# Well, not here, #}
{#                         this is the manually edited template file, #}
{#                                placing this message in the output. #}

{% set NS = ns|upper %}

#ifndef {{ NS }}_H
#define {{ NS }}_H 1

#ifdef __cplusplus
#include <string>
#include <vector>
#endif

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "common/cstring_view.h"

#pragma GCC diagnostic push
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wcast-align"
#endif

/* Beginning of extra_h */
{{ extra_h }}
/* End of extra_h */

#ifdef __cplusplus
extern "C" {
#endif

/* These are just so that you can "FBB_Builder *" or "FBB_Serialized *" instead of the more generic "void *",
 * resulting in nicer code. */
typedef struct {
  int {{ ns }}_tag;
} {{ NS }}_Builder;

typedef struct {
  int {{ ns }}_tag;
} {{ NS }}_Serialized;

typedef uint32_t fbb_size_t;

typedef enum {
  /* A standard plain C "char**" containing the strings pointers. */
  {{ NS }}_STRING_INPUT_FORMAT_ARRAY,
  /* A "cstring_view*" containing the (pointer, length) pairs. */
  {{ NS }}_STRING_INPUT_FORMAT_CSTRING_VIEW_ARRAY,
#ifdef __cplusplus
  /* C++ only: A "std::string*" pointing to the string array. */
  {{ NS }}_STRING_INPUT_FORMAT_CXX_STRING_ARRAY,
#endif
  /* An item_fn callback that returns the string pointer and length for a given index. */
  {{ NS }}_STRING_INPUT_FORMAT_CALLBACK,
} {{ NS }}_String_Input_Format;

typedef enum {
  /* A plain C-style array containing the FBB pointers as items. */
  {{ NS }}_FBB_INPUT_FORMAT_ARRAY,
  /* An item_fn callback that returns the FBB pointer for a given index. */
  {{ NS }}_FBB_INPUT_FORMAT_CALLBACK,
} {{ NS }}_FBB_Input_Format;

enum {
  /* Values are spelled out for easier debugging.
   * Start at 1 so that it's easier to catch a forgotten initialization. */
  {{ NS }}_TAG_UNUSED = 0,
### for (msg, _) in msgs
  {{ NS }}_TAG_{{ msg }} = {{ loop.index }},
### endfor
  {{ NS }}_TAG_NEXT
};

#ifdef __cplusplus
}  /* close extern "C" for the inline methods so that we can use C++ function overloading */
#endif

### for (msg, fields) in msgs
/******************************************************************************
 *  {{ msg }}
 ******************************************************************************/

###   set jinjans = namespace(has_relptr=False)
###   for (quant, type, var) in fields
###     if quant == ARRAY or type in [STRING, FBB]
###       set jinjans.has_relptr = True
###     endif
###   endfor

/***** Generic *****/

/*
 * Wire buffer, common to the Builder as well
 */
typedef struct _{{ NS }}_Serialized_{{ msg }} {
  /* It's important that the tag is the very first field */
  int {{ ns }}_tag;
  /* Required and optional scalar fields */
###   for (quant, type, var) in fields
###     if quant != ARRAY and type not in [STRING, FBB]
  {{ type }} {{ var }};
###     endif
###   endfor

  /* Required and optional string and FBB fields */
###   for (quant, type, var) in fields
###     if quant != ARRAY and type in [STRING, FBB]
###       if type == STRING
  fbb_size_t {{ var }}_len;
###       endif
###     endif
###   endfor

  /* Arrays of anything */
###   for (quant, type, var) in fields
###     if quant == ARRAY
  fbb_size_t {{ var }}_count;
###     endif
###   endfor

  /* Whether optional scalars have been set */
###   for (quant, type, var) in fields
###     if quant == OPTIONAL and type not in [STRING, FBB]
  bool has_{{ var }} : 1;
###     endif
###   endfor
} {{ NS }}_Serialized_{{ msg }};

/*
 * Placed in the serialized format after {{ NS }}_Serialized_{{ msg }},
 * containing the direct relptrs and the first hops of the indirect relptrs
 */
###   if jinjans.has_relptr
typedef struct _{{ NS }}_Relptrs_{{ msg }} {
###     for (quant, type, var) in fields
###       if quant == ARRAY or type in [STRING, FBB]
  fbb_size_t {{ var }}_relptr;
###       endif
###     endfor
} {{ NS }}_Relptrs_{{ msg }};
###   else
/* Empty {{ NS }}_Relptrs_{{ msg }} not defined becase C and C++ would disagree on its size */
###   endif

/*
 * Builder
 */
typedef struct _{{ NS }}_Builder_{{ msg }} {
  /* The part of the message that's common with the serialized format */
  {{ NS }}_Serialized_{{ msg }} wire;

  /* Arrays of scalars (pointers only, owned by the caller) */
###   for (quant, type, var) in fields
###     if quant == ARRAY and type not in [STRING, FBB]
  const {{ type }} *{{ var }};
###     endif
###   endfor

  /* Single strings and FBBs (pointers only, owned by the caller */
###   for (quant, type, var) in fields
###     if quant != ARRAY
###       if type == STRING
  const char *{{ var }};
###       elif type == FBB
  const {{ NS }}_Builder *{{ var }};
###       endif
###     endif
###   endfor

  /* Arrays of strings and FBBs (pointers only, owned by the caller) */
###   for (quant, type, var) in fields
###     if quant == ARRAY
###       if type == STRING
  /* In what format do we have the strings */
  {{ NS }}_String_Input_Format {{ var }}_how;
  union {
    /* For STRING_INPUT_FORMAT_C_ARRAY */
    const char * const *c_array;
    /* For STRING_INPUT_FORMAT_CSTRING_VIEW_ARRAY */
    const cstring_view *cstring_view_array;
#ifdef __cplusplus
    /* For STRING_INPUT_FORMAT_CXX_STRING_ARRAY */
    const std::string *cxx_string_array;
#endif
    /* For STRING_INPUT_FORMAT_CALLBACK */
    struct {
      /* Function to get the Nth item of the string array */
      const char * (*item_fn) (int idx, const void *user_data, fbb_size_t *len_out);
      /* Arbitrary pointer passed to item_fn */
      const void *user_data;
    } callback;
  } {{ var }};
###       elif type == FBB
  /* In what format do we have the strings */
  {{ NS }}_FBB_Input_Format {{ var }}_how;
  union {
    /* For FBB_INPUT_FORMAT_ARRAY */
    const {{ NS }}_Builder * const *c_array;
    /* For FBB_INPUT_FORMAT_CALLBACK */
    struct {
      /* Function to get the Nth item of the FBB array */
      const {{ NS }}_Builder * (*item_fn) (int idx, const void *user_data);
      /* Arbitrary pointer passed to item_fn */
      const void *user_data;
    } callback;
  } {{ var }};
###       endif
###     endif
###   endfor

#ifndef NDEBUG
  /* Whether required scalars have been set */
###   for (quant, type, var) in fields
###     if type not in [STRING, FBB] and quant == REQUIRED
  bool has_{{ var }} : 1;
###     endif
###   endfor
#endif
} {{ NS }}_Builder_{{ msg }};

/*
 * Builder: Initialize, set tag
 */
static inline void {{ ns }}_builder_{{ msg }}_init({{ NS }}_Builder_{{ msg }} *msg) {
  memset(msg, 0, sizeof(*msg));
  msg->wire.{{ ns }}_tag = {{ NS }}_TAG_{{ msg }};
}

/***** Setters *****/

###   for (quant, type, var) in fields
{% set ctype = "const char *" if type == STRING else "const " + NS + "_Builder *" if type == FBB else type %}
###     if type not in [STRING, FBB]
###       if quant == REQUIRED
/*
 * Builder setter - required scalar
 * {{ type }} {{ var }}
 */
static inline void {{ ns }}_builder_{{ msg }}_set_{{ var }}({{ NS }}_Builder_{{ msg }} *msg, {{ type }} value) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  msg->wire.{{ var }} = value;
#ifndef NDEBUG
  msg->has_{{ var }} = true;
#endif
}
###       elif quant == OPTIONAL
/*
 * Builder setter - optional scalar
 * {{ type }} {{ var }}
 */
static inline void {{ ns }}_builder_{{ msg }}_set_{{ var }}({{ NS }}_Builder_{{ msg }} *msg, {{ type }} value) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  msg->wire.{{ var }} = value;
  msg->wire.has_{{ var }} = true;
}
###       else
/*
 * Builder setter - array of scalars
 * {{ type }}[] {{ var }}
 */
static inline void {{ ns }}_builder_{{ msg }}_set_{{ var }}({{ NS }}_Builder_{{ msg }} *msg, const {{ type }} *values, fbb_size_t count) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  msg->{{ var }} = values;
  msg->wire.{{ var }}_count = count;
}
#ifdef __cplusplus
/*
 * Builder setter - array of scalars (C++)
 * {{ type }}[] {{ var }}
 */
static inline void {{ ns }}_builder_{{ msg }}_set_{{ var }}({{ NS }}_Builder_{{ msg }} *msg, const std::vector<{{ type }}>& values) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  msg->{{ var }} = values.data();
  msg->wire.{{ var }}_count = values.size();
}
#endif
###       endif
###     else
###       if quant in [REQUIRED, OPTIONAL]
###         if type == STRING
/*
 * Builder setter - required or optional string with length
 * {{ type }} {{ var }}
 */
static inline void {{ ns }}_builder_{{ msg }}_set_{{ var }}_with_length({{ NS }}_Builder_{{ msg }} *msg, const char *value, fbb_size_t len) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});
  assert(value == NULL || strlen(value) == len);  /* if len is specified, it must be the correct value */

  msg->{{ var }} = value;
  msg->wire.{{ var }}_len = len;
}
/*
 * Builder setter - required or optional string
 * {{ type }} {{ var }}
 */
static inline void {{ ns }}_builder_{{ msg }}_set_{{ var }}({{ NS }}_Builder_{{ msg }} *msg, const char *value) {
  {{ ns }}_builder_{{ msg }}_set_{{ var }}_with_length(msg, value, value ? strlen(value) : 0);
}
#ifdef __cplusplus
/*
 * Builder setter - required or optional string (C++)
 * {{ type }} {{ var }}
 */
static inline void {{ ns }}_builder_{{ msg }}_set_{{ var }}({{ NS }}_Builder_{{ msg }} *msg, const std::string& value) {
  {{ ns }}_builder_{{ msg }}_set_{{ var }}_with_length(msg, value.c_str(), value.length());
}
#endif
###         else
/*
 * Builder setter - required or optional FBB
 * {{ type }} {{ var }}
 */
static inline void {{ ns }}_builder_{{ msg }}_set_{{ var }}({{ NS }}_Builder_{{ msg }} *msg, const {{ NS }}_Builder *value) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  msg->{{ var }} = value;
}
###         endif
###       else
/*
 * Builder setter - array of strings or FBBs with item count
 * {{ type }}[] {{ var }}
 */
static inline void {{ ns }}_builder_{{ msg }}_set_{{ var }}_with_count({{ NS }}_Builder_{{ msg }} *msg, {{ ctype }} const *values, fbb_size_t count) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  msg->{{ var }}_how = {{ NS }}_{{ type|upper }}_INPUT_FORMAT_ARRAY;
  msg->{{ var }}.c_array = values;
  msg->wire.{{ var }}_count = count;
}
/*
 * Builder setter - array of strings or FBBs
 * {{ type }}[] {{ var }}
 */
static inline void {{ ns }}_builder_{{ msg }}_set_{{ var }}({{ NS }}_Builder_{{ msg }} *msg, {{ ctype }} const *values) {
  fbb_size_t count = 0;
  if (values != NULL) {
    while (values[count] != NULL) count++;
  }
  {{ ns }}_builder_{{ msg }}_set_{{ var }}_with_count(msg, values, count);
}
###         if type == STRING
/*
 * Builder setter - array of strings as cstring_view
 * {{ type }}[] {{ var }}
 */
static inline void {{ ns }}_builder_{{ msg }}_set_{{ var }}_cstring_views({{ NS }}_Builder_{{ msg }} *msg, const cstring_view *values, fbb_size_t count) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  msg->{{ var }}_how = {{ NS }}_STRING_INPUT_FORMAT_CSTRING_VIEW_ARRAY;
  msg->{{ var }}.cstring_view_array = values;
  msg->wire.{{ var }}_count = count;
}
#ifdef __cplusplus
/*
 * Builder setter - array of strings as vector<cstring_view> (C++)
 * {{ type }}[] {{ var }}
 */
static inline void {{ ns }}_builder_{{ msg }}_set_{{ var }}({{ NS }}_Builder_{{ msg }} *msg, const std::vector<cstring_view>& values) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  msg->{{ var }}_how = {{ NS }}_STRING_INPUT_FORMAT_CSTRING_VIEW_ARRAY;
  msg->{{ var }}.cstring_view_array = values.data();
  msg->wire.{{ var }}_count = values.size();
}
#endif
###         endif
#ifdef __cplusplus
/*
 * Builder setter - array of strings or FBBs (C++)
 * {{ type }}[] {{ var }}
 */
static inline void {{ ns }}_builder_{{ msg }}_set_{{ var }}({{ NS }}_Builder_{{ msg }} *msg, const std::vector<{{ ctype }}>& values) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  msg->{{ var }}_how = {{ NS }}_{{ type|upper }}_INPUT_FORMAT_ARRAY;
  msg->{{ var }}.c_array = values.data();
  msg->wire.{{ var }}_count = values.size();
}
###         if type == STRING
/*
 * Builder setter - array of strings as vector<string> (C++)
 * {{ type }}[] {{ var }}
 */
static inline void {{ ns }}_builder_{{ msg }}_set_{{ var }}({{ NS }}_Builder_{{ msg }} *msg, const std::vector<std::string>& values) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  msg->{{ var }}_how = {{ NS }}_STRING_INPUT_FORMAT_CXX_STRING_ARRAY;
  msg->{{ var }}.cxx_string_array = values.data();
  msg->wire.{{ var }}_count = values.size();
}
###         endif
#endif
/*
 * Builder setter - array of strings or FBBs as an item getter function
 * {{ type }}[] {{ var }}
 */
static inline void {{ ns }}_builder_{{ msg }}_set_{{ var }}_item_fn({{ NS }}_Builder_{{ msg }} *msg, fbb_size_t count, {{ ctype }} (* item_fn) (int idx, const void *user_data{% if type == STRING %}, fbb_size_t *len_out{% endif %}), const void *user_data) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  msg->{{ var }}_how = {{ NS }}_{{ type|upper }}_INPUT_FORMAT_CALLBACK;
  msg->{{ var }}.callback.item_fn = item_fn;
  msg->{{ var }}.callback.user_data = user_data;
  msg->wire.{{ var }}_count = count;
}
###       endif
###     endif
###   endfor

/***** Getters on the builder *****/

###   for (quant, type, var) in fields
{% set ctype = "const char *" if type == STRING else "const " + NS + "_Builder *" if type == FBB else type %}
###     if quant == OPTIONAL
/*
 * Builder getter - check if optional field is set
 * {{ type }} {{ var }}
 */
static inline bool {{ ns }}_builder_{{ msg }}_has_{{ var }}(const {{ NS }}_Builder_{{ msg }} *msg) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

###       if type in [STRING, FBB]
  return msg->{{ var }} != NULL;
###       else
  return msg->wire.has_{{ var }};
###       endif
}
###     endif

###     if quant in [REQUIRED, OPTIONAL]
###       if type not in [STRING, FBB]
/*
 * Builder getter - required or optional scalar
 * {{ type }} {{ var }}
 */
static inline {{ type }} {{ ns }}_builder_{{ msg }}_get_{{ var }}(const {{ NS }}_Builder_{{ msg }} *msg) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

###         if quant == OPTIONAL
  assert(msg->wire.has_{{ var }});
###         endif
  return msg->wire.{{ var }};
}
###         if quant == OPTIONAL
/*
 * Builder getter - optional scalar with fallback default
 * {{ type }} {{ var }}
 */
static inline {{ type }} {{ ns }}_builder_{{ msg }}_get_{{ var }}_with_fallback(const {{ NS }}_Builder_{{ msg }} *msg, {{ type }} fallback) {
  return msg->wire.has_{{ var }} ? msg->wire.{{ var }} : fallback;
}
###         endif
###       else
/*
 * Builder getter - required or optional string or FBB
 * {{ type }} {{ var }}
 */
static inline {{ ctype }} {{ ns }}_builder_{{ msg }}_get_{{ var }}(const {{ NS }}_Builder_{{ msg }} *msg) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  return msg->{{ var }};
}
###         if type == STRING
/*
 * Builder getter - required or optional string's length
 * {{ type }} {{ var }}
 */
static inline fbb_size_t {{ ns }}_builder_{{ msg }}_get_{{ var }}_len(const {{ NS }}_Builder_{{ msg }} *msg) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  return msg->wire.{{ var }}_len;
}
/*
 * Builder getter - required or optional string along with its length
 * {{ type }} {{ var }}
 */
static inline {{ ctype }} {{ ns }}_builder_{{ msg }}_get_{{ var }}_with_len(const {{ NS }}_Builder_{{ msg }} *msg, fbb_size_t *len_out) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  *len_out = {{ ns }}_builder_{{ msg }}_get_{{ var }}_len(msg);
  return {{ ns }}_builder_{{ msg }}_get_{{ var }}(msg);
}
#ifdef __cplusplus
/*
 * Builder getter - required or optional string (C++, not async-signal-safe)
 * {{ type }} {{ var }}
 */
static inline std::string {{ ns }}_builder_{{ msg }}_get_{{ var }}_as_string(const {{ NS }}_Builder_{{ msg }} *msg) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});
  assert(msg->{{ var }} != NULL);

  return std::string(msg->{{ var }}, msg->wire.{{ var }}_len);
}
#endif
###         endif
###       endif
###     else
/*
 * Builder getter - array item count
 * {{ type }}[] {{ var }}
 */
static inline fbb_size_t {{ ns }}_builder_{{ msg }}_get_{{ var }}_count(const {{ NS }}_Builder_{{ msg }} *msg) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  return msg->wire.{{ var }}_count;
}
###       if type not in [STRING, FBB]
/*
 * Builder getter - array of scalars
 * {{ type }}[] {{ var }}
 */
static inline const {{ type }} *{{ ns }}_builder_{{ msg }}_get_{{ var }}(const {{ NS }}_Builder_{{ msg }} *msg) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  return msg->{{ var }};
}
###       endif
/*
 * Builder getter - one item from an array
 * {{ type }}[] {{ var }}
 */
static inline {{ ctype }} {{ ns }}_builder_{{ msg }}_get_{{ var }}_at(const {{ NS }}_Builder_{{ msg }} *msg, fbb_size_t idx) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});
  assert(idx < msg->wire.{{ var }}_count);

###       if type not in [STRING, FBB]
  return msg->{{ var }}[idx];
###       elif type == STRING
  switch (msg->{{ var }}_how) {
    case {{ NS }}_STRING_INPUT_FORMAT_ARRAY:
      return msg->{{ var }}.c_array[idx];
    case {{ NS }}_STRING_INPUT_FORMAT_CSTRING_VIEW_ARRAY:
      return msg->{{ var }}.cstring_view_array[idx].c_str;
#ifdef __cplusplus
    case {{ NS }}_STRING_INPUT_FORMAT_CXX_STRING_ARRAY:
      return msg->{{ var }}.cxx_string_array[idx].c_str();
#endif
    case {{ NS }}_STRING_INPUT_FORMAT_CALLBACK:
      return (msg->{{ var }}.callback.item_fn)(idx, msg->{{ var }}.callback.user_data, NULL);
  }
  assert(0);
  return NULL;
###       else
  switch (msg->{{ var }}_how) {
    case {{ NS }}_FBB_INPUT_FORMAT_ARRAY:
      return msg->{{ var }}.c_array[idx];
    case {{ NS }}_FBB_INPUT_FORMAT_CALLBACK:
      return (msg->{{ var }}.callback.item_fn)(idx, msg->{{ var }}.callback.user_data);
  }
  assert(0);
  return NULL;
###       endif
}
###       if type == STRING
/*
 * Builder getter - one item's length from a string array
 * {{ type }}[] {{ var }}
 */
static inline fbb_size_t {{ ns }}_builder_{{ msg }}_get_{{ var }}_len_at(const {{ NS }}_Builder_{{ msg }} *msg, fbb_size_t idx) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});
  assert(idx < msg->wire.{{ var }}_count);

  switch (msg->{{ var }}_how) {
    case {{ NS }}_STRING_INPUT_FORMAT_ARRAY:
      /* This is costly, the requested length is not readily available so we have to compute it. */
      return strlen(msg->{{ var }}.c_array[idx]);
    case {{ NS }}_STRING_INPUT_FORMAT_CSTRING_VIEW_ARRAY:
      return msg->{{ var }}.cstring_view_array[idx].length;
#ifdef __cplusplus
    case {{ NS }}_STRING_INPUT_FORMAT_CXX_STRING_ARRAY:
      return msg->{{ var }}.cxx_string_array[idx].length();
#endif
    case {{ NS }}_STRING_INPUT_FORMAT_CALLBACK: {
      fbb_size_t len;
      (msg->{{ var }}.callback.item_fn)(idx, msg->{{ var }}.callback.user_data, &len);
      return len;
    }
  }
  assert(0);
  return 0;
}
/*
 * Builder getter - one item from a string array along with its length
 * {{ type }}[] {{ var }}
 */
static inline {{ ctype }} {{ ns }}_builder_{{ msg }}_get_{{ var }}_with_len_at(const {{ NS }}_Builder_{{ msg }} *msg, fbb_size_t idx, fbb_size_t *len_out) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});
  assert(idx < msg->wire.{{ var }}_count);

  switch (msg->{{ var }}_how) {
    case {{ NS }}_STRING_INPUT_FORMAT_ARRAY:
      /* This is costly, the requested length is not readily available so we have to compute it. */
      *len_out = strlen(msg->{{ var }}.c_array[idx]);
      return msg->{{ var }}.c_array[idx];
    case {{ NS }}_STRING_INPUT_FORMAT_CSTRING_VIEW_ARRAY:
      *len_out = msg->{{ var }}.cstring_view_array[idx].length;
      return msg->{{ var }}.cstring_view_array[idx].c_str;
#ifdef __cplusplus
    case {{ NS }}_STRING_INPUT_FORMAT_CXX_STRING_ARRAY:
      *len_out = msg->{{ var }}.cxx_string_array[idx].length();
      return msg->{{ var }}.cxx_string_array[idx].c_str();
#endif
    case {{ NS }}_STRING_INPUT_FORMAT_CALLBACK:
      return (msg->{{ var }}.callback.item_fn)(idx, msg->{{ var }}.callback.user_data, len_out);
  }
  assert(0);
  return NULL;
}
###       endif
#ifdef __cplusplus
/*
 * Builder getter - array (C++, not async-signal-safe)
 * {{ type }}[] {{ var }}
 */
static inline std::vector<{{ "std::string" if type == STRING else ctype }}> {{ ns }}_builder_{{ msg }}_get_{{ var }}_as_vector(const {{ NS }}_Builder_{{ msg }} *msg) {
  assert(msg->wire.{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  std::vector<{{ "std::string" if type == STRING else ctype }}> ret;
  ret.reserve(msg->wire.{{ var }}_count);
  for (fbb_size_t idx = 0; idx < msg->wire.{{ var }}_count; idx++)
    ret.emplace_back({{ ns }}_builder_{{ msg }}_get_{{ var }}_at(msg, idx));
  return ret;
}
#endif
###     endif
###   endfor

/***** Getters on the serialized format *****/

###   for (quant, type, var) in fields
{% set ctype = "const char *" if type == STRING else "const " + NS + "_Serialized *" if type == FBB else type %}
###     if quant == OPTIONAL
/*
 * Serialized getter - check if optional field is set
 * {{ type }} {{ var }}
 */
static inline bool {{ ns }}_serialized_{{ msg }}_has_{{ var }}(const {{ NS }}_Serialized_{{ msg }} *msg) {
  assert(msg->{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

###       if type in [STRING, FBB]
  const {{ NS }}_Relptrs_{{ msg }} *relptrs = (const {{ NS }}_Relptrs_{{ msg }} *) ((const {{ NS }}_Serialized_{{ msg }} *) &msg[1]);  /* the area immediately followed by the {{ NS }}_Serialized_{{ msg }} structure */
  return relptrs->{{ var }}_relptr != 0;
###       else
  return msg->has_{{ var }};
###       endif
}
###     endif

###     if quant in [REQUIRED, OPTIONAL]
###       if type not in [STRING, FBB]
/*
 * Serialized getter - required or optional scalar
 * {{ type }} {{ var }}
 */
static inline {{ type }} {{ ns }}_serialized_{{ msg }}_get_{{ var }}(const {{ NS }}_Serialized_{{ msg }} *msg) {
  assert(msg->{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

###         if quant == OPTIONAL
  assert(msg->has_{{ var }});
###         endif
  return msg->{{ var }};
}
###         if quant == OPTIONAL
/*
 * Serialized getter - optional scalar with fallback default
 * {{ type }} {{ var }}
 */
static inline {{ type }} {{ ns }}_serialized_{{ msg }}_get_{{ var }}_with_fallback(const {{ NS }}_Serialized_{{ msg }} *msg, {{ type }} fallback) {
  return msg->has_{{ var }} ? msg->{{ var }} : fallback;
}
###         endif
###       else
/*
 * Serialized getter - required or optional string or FBB
 * {{ type }} {{ var }}
 */
static inline {{ ctype }} {{ ns }}_serialized_{{ msg }}_get_{{ var }}(const {{ NS }}_Serialized_{{ msg }} *msg) {
  assert(msg->{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  const {{ NS }}_Relptrs_{{ msg }} *relptrs = (const {{ NS }}_Relptrs_{{ msg }} *) ((const {{ NS }}_Serialized_{{ msg }} *) &msg[1]);  /* the area immediately followed by the {{ NS }}_Serialized_{{ msg }} structure */
  if (relptrs->{{ var }}_relptr == 0) {
###         if quant == REQUIRED
    assert(relptrs->{{ var }}_relptr != 0);
###         else
    return NULL;
###         endif
  }
  const char *ret = (const char *)msg + relptrs->{{ var }}_relptr;
  return ({{ ctype }}) ret;
}
###         if type == STRING
/*
 * Serialized getter - required or optional string's length
 * {{ type }} {{ var }}
 */
static inline fbb_size_t {{ ns }}_serialized_{{ msg }}_get_{{ var }}_len(const {{ NS }}_Serialized_{{ msg }} *msg) {
  assert(msg->{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  return msg->{{ var }}_len;
}
/*
 * Serialized getter - required or optional string along with its length
 * {{ type }} {{ var }}
 */
static inline {{ ctype }} {{ ns }}_serialized_{{ msg }}_get_{{ var }}_with_len(const {{ NS }}_Serialized_{{ msg }} *msg, fbb_size_t *len_out) {
  assert(msg->{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  *len_out = {{ ns }}_serialized_{{ msg }}_get_{{ var }}_len(msg);
  return {{ ns }}_serialized_{{ msg }}_get_{{ var }}(msg);
}
#ifdef __cplusplus
/*
 * Serialized getter - required or optional string (C++, not async-signal-safe)
 * {{ type }} {{ var }}
 */
static inline std::string {{ ns }}_serialized_{{ msg }}_get_{{ var }}_as_string(const {{ NS }}_Serialized_{{ msg }} *msg) {
  assert(msg->{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  const char *c_str = {{ ns }}_serialized_{{ msg }}_get_{{ var }}(msg);
  assert(c_str != NULL);
  return std::string(c_str, msg->{{ var }}_len);
}
#endif
###         endif
###       endif
###     else
/*
 * Serialized getter - array item count
 * {{ type }}[] {{ var }}
 */
static inline fbb_size_t {{ ns }}_serialized_{{ msg }}_get_{{ var }}_count(const {{ NS }}_Serialized_{{ msg }} *msg) {
  assert(msg->{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  return msg->{{ var }}_count;
}
###       if type not in [STRING, FBB]
/*
 * Serialized getter - array of scalars
 * {{ type }}[] {{ var }}
 */
static inline const {{ type }} *{{ ns }}_serialized_{{ msg }}_get_{{ var }}(const {{ NS }}_Serialized_{{ msg }} *msg) {
  assert(msg->{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  const {{ NS }}_Relptrs_{{ msg }} *relptrs = (const {{ NS }}_Relptrs_{{ msg }} *) ((const {{ NS }}_Serialized_{{ msg }} *) &msg[1]);  /* the area immediately followed by the {{ NS }}_Serialized_{{ msg }} structure */
  const char *array = (const char *)msg + relptrs->{{ var }}_relptr;
  return (const {{ type }} *) array;
}
/*
 * Serialized getter - one item from an array of scalars
 * {{ type }}[] {{ var }}
 */
static inline {{ type }} {{ ns }}_serialized_{{ msg }}_get_{{ var }}_at(const {{ NS }}_Serialized_{{ msg }} *msg, fbb_size_t idx) {
  assert(msg->{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});
#ifndef NDEBUG
  assert(idx < msg->{{ var }}_count);
#endif

  const {{ NS }}_Relptrs_{{ msg }} *relptrs = (const {{ NS }}_Relptrs_{{ msg }} *) ((const {{ NS }}_Serialized_{{ msg }} *) &msg[1]);  /* the area immediately followed by the {{ NS }}_Serialized_{{ msg }} structure */
  const void *array_void = (const char *)msg + relptrs->{{ var }}_relptr;
  const {{ type }} *array = (const {{ type }} *)array_void;
  return array[idx];
}
###       else
/*
 * Serialized getter - one item from an array of strings or FBBs
 * {{ type }}[] {{ var }}
 */
static inline {{ ctype }} {{ ns }}_serialized_{{ msg }}_get_{{ var }}_at(const {{ NS }}_Serialized_{{ msg }} *msg, fbb_size_t idx) {
  assert(msg->{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});
#ifndef NDEBUG
  assert(idx < msg->{{ var }}_count);
#endif

  /* double jump */
  const {{ NS }}_Relptrs_{{ msg }} *relptrs = (const {{ NS }}_Relptrs_{{ msg }} *) ((const {{ NS }}_Serialized_{{ msg }} *) &msg[1]);  /* the area immediately followed by the {{ NS }}_Serialized_{{ msg }} structure */
  const void *second_relptrs_void = (const char *)msg + relptrs->{{ var }}_relptr;
  const fbb_size_t *second_relptrs = (const fbb_size_t *)second_relptrs_void;
  const char *ret = (const char *)msg + second_relptrs[{{ "2 * " if type == STRING }}idx];
  return ({{ ctype }}) ret;
}
###         if type == STRING
/*
 * Serialized getter - one item's length from an array of strings
 * {{ type }}[] {{ var }}
 */
static inline fbb_size_t {{ ns }}_serialized_{{ msg }}_get_{{ var }}_len_at(const {{ NS }}_Serialized_{{ msg }} *msg, fbb_size_t idx) {
  assert(msg->{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});
#ifndef NDEBUG
  assert(idx < msg->{{ var }}_count);
#endif

  const {{ NS }}_Relptrs_{{ msg }} *relptrs = (const {{ NS }}_Relptrs_{{ msg }} *) ((const {{ NS }}_Serialized_{{ msg }} *) &msg[1]);  /* the area immediately followed by the {{ NS }}_Serialized_{{ msg }} structure */
  const void *second_relptrs_void = (const char *)msg + relptrs->{{ var }}_relptr;
  const fbb_size_t *second_relptrs = (const fbb_size_t *)second_relptrs_void;
  return second_relptrs[2 * idx + 1];
}
/*
 * Serialized getter - one item along with its length from an array of strings
 * {{ type }}[] {{ var }}
 */
static inline {{ ctype }} {{ ns }}_serialized_{{ msg }}_get_{{ var }}_with_len_at(const {{ NS }}_Serialized_{{ msg }} *msg, fbb_size_t idx, fbb_size_t *len_out) {
  assert(msg->{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});
#ifndef NDEBUG
  assert(idx < msg->{{ var }}_count);
#endif

  *len_out = {{ ns }}_serialized_{{ msg }}_get_{{ var }}_len_at(msg, idx);
  return {{ ns }}_serialized_{{ msg }}_get_{{ var }}_at(msg, idx);
}
###         endif
###       endif
#ifdef __cplusplus
/*
 * Serialized getter - array (C++, not async-signal-safe)
 * {{ type }}[] {{ var }}
 */
static inline std::vector<{{ "std::string" if type == STRING else ctype }}> {{ ns }}_serialized_{{ msg }}_get_{{ var }}_as_vector(const {{ NS }}_Serialized_{{ msg }} *msg) {
  assert(msg->{{ ns }}_tag == {{ NS }}_TAG_{{ msg }});

  std::vector<{{ "std::string" if type == STRING else ctype }}> ret;
  ret.reserve( msg->{{ var }}_count);
  for (fbb_size_t idx = 0; idx < msg->{{ var }}_count; idx++) {
    ret.emplace_back({{ ns }}_serialized_{{ msg }}_get_{{ var }}_at(msg, idx) {% if type == STRING %}, (size_t){{ ns }}_serialized_{{ msg }}_get_{{ var }}_len_at(msg, idx) {% endif %});
  }
  return ret;
}
#endif
###     endif
###   endfor
### endfor

/******************************************************************************
 *  Global
 ******************************************************************************/
#ifdef __cplusplus
extern "C" {
#endif

/*
 * Get the tag from the builder
 */
static inline int {{ ns }}_builder_get_tag(const {{ NS }}_Builder *msg) {
  return msg->{{ ns }}_tag;
}

/*
 * Get the tag from the serialized version
 */
static inline int {{ ns }}_serialized_get_tag(const {{ NS }}_Serialized *msg) {
  return msg->{{ ns }}_tag;
}

/*
 * Get the tag as string
 */
const char *{{ ns }}_tag_to_string(int tag);

/*
 * Builder - Debug any message
 *
 * Generate valid JSON (and almost valid Python - just set null=None before parsing it)
 * so that it's easier to postprocess with random tools.
 */
void {{ ns }}_builder_debug(FILE *f, const {{ NS }}_Builder *msg);

/*
 * Serialized - Debug any message
 *
 * Generate valid JSON (and almost valid Python - just set null=None before parsing it)
 * so that it's easier to postprocess with random tools.
 */
void {{ ns }}_serialized_debug(FILE *f, const {{ NS }}_Serialized *msg);

/*
 * Builder - Measure any message
 *
 * Return the length of the serialized form.
 */
fbb_size_t {{ ns }}_builder_measure(const {{ NS }}_Builder *msg);

/*
 * Builder - Serialize any message to memory
 *
 * Takes a buffer that is large enough to hold the serialized form, as guaranteed by a preceding {{ ns }}_builder_measure() call.
 *
 * Return the length of the serialized form.
 */
fbb_size_t {{ ns }}_builder_serialize(const {{ NS }}_Builder *msg, char *dst);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#pragma GCC diagnostic pop

#endif  /* {{ NS }}_H */
