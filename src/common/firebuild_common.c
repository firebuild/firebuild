/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "common/firebuild_common.h"

#ifdef __cplusplus
extern "C" {
#endif

void string_array_init(string_array *array) {
  memset(array, 0, sizeof(*array));
}

/* Does NOT deep copy the string */
void string_array_append(string_array *array, char *s) {
  if (array->size_alloc == 0) {
    array->size_alloc = 16  /* whatever */;
    array->p = malloc(sizeof(char *) * array->size_alloc);
  } else if (array->len + 1 == array->size_alloc) {
    array->size_alloc *= 2;
    array->p = realloc(array->p, sizeof(char *) * array->size_alloc);
  }
  array->p[array->len++] = s;
  array->p[array->len] = NULL;
}

void string_array_deep_free(string_array *array) {
  for (int i = 0; i < array->len; i++)
    free(array->p[i]);
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
bool is_path_at_locations(const char * const path, string_array *location_array) {
  const size_t path_len = strlen(path);
  for (int i = 0; i < location_array->len; i++) {
    const char * const location = location_array->p[i];
    size_t location_len = strlen(location);
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

