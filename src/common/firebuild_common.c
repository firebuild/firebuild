/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "common/firebuild_common.h"

#ifdef __cplusplus
extern "C" {
#endif

void cstring_view_array_init(cstring_view_array *array) {
  memset(array, 0, sizeof(*array));
}

/* Does NOT deep copy the string */
void cstring_view_array_append(cstring_view_array *array, char *s) {
  if (array->size_alloc == 0) {
    array->size_alloc = 16  /* whatever */;
    array->p = malloc(sizeof(cstring_view) * array->size_alloc);
  } else if (array->len + 1 == array->size_alloc) {
    array->size_alloc *= 2;
    array->p = realloc(array->p, sizeof(cstring_view) * array->size_alloc);
  }
  array->p[array->len].c_str = s;
  array->p[array->len].length = strlen(s);
  array->len++;
  array->p[array->len].c_str = NULL;
}

/* The string array needs to allocate more space to append a new entry. */
bool is_cstring_view_array_full(cstring_view_array *array) {
  return !array || array->len == array->size_alloc - 1;
}

/* Does NOT deep copy the string */
void cstring_view_array_append_noalloc(cstring_view_array *array, char *s) {
  assert(array->size_alloc > 0);
  assert(array->len + 1 < array->size_alloc);
  array->p[array->len].c_str = s;
  array->p[array->len].length = strlen(s);
  array->len++;
  array->p[array->len].c_str = NULL;
}

void cstring_view_array_deep_free(cstring_view_array *array) {
  for (int i = 0; i < array->len; i++)
    free((/* non-const */ char *)array->p[i].c_str);
  free(array->p);
}

void voidp_array_init(voidp_array *array) {
  memset(array, 0, sizeof(*array));
}

/* Does NOT deep copy whatever is behind voidp - obviously */
void voidp_array_append(voidp_array *array, void *p) {
  if (array->size_alloc == 0) {
    array->size_alloc = 16  /* whatever */;
    array->p = malloc(sizeof(void *) * array->size_alloc);
  } else if (array->len + 1 == array->size_alloc) {
    array->size_alloc *= 2;
    array->p = realloc(array->p, sizeof(void *) * array->size_alloc);
  }
  array->p[array->len++] = p;
  array->p[array->len] = NULL;
}

void voidp_array_deep_free(voidp_array *array, void (*fn_free)(void *)) {
  if (fn_free != NULL) {
    for (int i = 0; i < array->len; i++) {
      (*fn_free)(array->p[i]);
    }
  }
  free(array->p);
}

void voidp_set_init(voidp_set *set) {
  memset(set, 0, sizeof(*set));
}

void voidp_set_clear(voidp_set *set) {
  set->len = 0;
}

bool voidp_set_contains(const voidp_set *set, const void *p) {
  for (int i = 0; i < set->len; i++) {
    if (set->p[i] == p) {
      return true;
    }
  }
  return false;
}

/* Does NOT deep copy whatever is behind voidp - obviously */
void voidp_set_insert(voidp_set *set, const void *p) {
  if (!voidp_set_contains(set, p)) {
    if (set->size_alloc == 0) {
      set->size_alloc = 16  /* whatever */;
      set->p = malloc(sizeof(void *) * set->size_alloc);
    } else if (set->len == set->size_alloc) {
      set->size_alloc *= 2;
      set->p = realloc(set->p, sizeof(void *) * set->size_alloc);
    }
    set->p[set->len++] = p;
  }
}

/* Does NOT deep free whatever is behind voidp - obviously */
void voidp_set_erase(voidp_set *set, const void *p) {
  for (int i = 0; i < set->len; i++) {
    if (set->p[i] == p) {
      set->p[i] = set->p[set->len - 1];
      set->len--;
      break;
    }
  }
}

/**
 * Checks if a path semantically begins with the given subpath.
 *
 * Does string operations only, does not look at the file system.
 */
bool is_path_at_locations(const char * const path, cstring_view_array *location_array) {
  const size_t path_len = strlen(path);
  for (int i = 0; i < location_array->len; i++) {
    const char * const location = location_array->p[i].c_str;
    size_t location_len = location_array->p[i].length;
    while (location_len > 0 && location[location_len - 1] == '/') {
      location_len--;
    }

    if (path_len < location_len) {
      continue;
    }

    if (memcmp(location, path, location_len) != 0) {
      continue;
    }

    if (path_len == location_len) {
      return true;
    }

    if (path[location_len] == '/') {
      return true;
    }
  }
  return false;
}

/**
 * Checks if the file name is canonical, i.e.:
 * - does not start with "./"
 * - does not end with "/" or "/."
 * - does not contain "//" or "/./"
 * - can contain "/../", since they might point elsewhere if a symlink led to its containing
 *    directory.
 *  See #401 for further details and gotchas.
 *
 * Returns if the path is in canonical form
 */
bool is_canonical(const char * const path, const size_t length) {
  if (path[0] == '\0') return true;
  if ((path[0] == '.' && path[1] == '/')
      || (length >= 2 && path[length - 1] == '/')
      || (length >= 2 && path[length - 2] == '/' && path[length - 1] == '.')
      || strstr(path, "//")
      || strstr(path, "/./")) {
    return false;
  }
  return true;
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

