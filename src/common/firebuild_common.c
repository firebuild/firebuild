/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 *
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 * Modification and redistribution are permitted, but commercial use of derivative
 * works is subject to the same requirements of this license
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

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
  } else if (array->len == array->size_alloc) {
    array->size_alloc *= 2;
    array->p = realloc(array->p, sizeof(cstring_view) * array->size_alloc);
  }
  array->p[array->len].c_str = s;
  array->p[array->len].length = strlen(s);
  array->len++;
}

static int cmp_cstring_view(const void *p1, const void *p2) {
  return strcmp(((const cstring_view * const)p1)->c_str,
                ((const cstring_view * const)p2)->c_str);
}

void cstring_view_array_sort(cstring_view_array *array) {
  if (array->p) {
    qsort(array->p, array->len, sizeof(cstring_view), cmp_cstring_view);
  }
}

/* The string array needs to allocate more space to append a new entry. */
bool is_cstring_view_array_full(cstring_view_array *array) {
  return !array || array->len == array->size_alloc;
}

/* Does NOT deep copy the string */
void cstring_view_array_append_noalloc(cstring_view_array *array, char *s) {
  assert(array->size_alloc > 0);
  assert(array->len < array->size_alloc);
  array->p[array->len].c_str = s;
  array->p[array->len].length = strlen(s);
  array->len++;
}

void cstring_view_array_deep_free(cstring_view_array *array) {
  for (int i = 0; i < array->len; i++)
    free((/* non-const */ char *)array->p[i].c_str);
  free(array->p);
}

bool is_in_sorted_cstring_view_array(const char *str, const ssize_t len,
                                     const cstring_view_array *array) {
  for (int i = 0; i < array->len; i++) {
    const char * const entry = array->p[i].c_str;
    const ssize_t entry_len = array->p[i].length;
    if (len != entry_len) {
      continue;
    }
    const int memcmp_res = memcmp(entry, str, len);
    if (memcmp_res < 0) {
      continue;
    } else {
      return memcmp_res == 0;
    }
  }
  return false;
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

/** Compare pointers to char* like strcmp() for char* */
int cmpstringpp(const void *p1, const void *p2) {
  /* The actual arguments to this function are "pointers to
     pointers to char", but strcmp(3) arguments are "pointers
     to char", hence the following cast plus dereference */
  return strcmp(* (char * const *) p1, * (char * const *) p2);
}

/**
 * Checks if a path semantically begins with one of the given sorted subpaths.
 *
 * Does string operations only, does not look at the file system.
 */
bool is_path_at_locations(const char * const path, const ssize_t len,
                          const cstring_view_array *location_array) {
  const ssize_t path_len = len >= 0 ? len : (ssize_t)strlen(path);
  for (int i = 0; i < location_array->len; i++) {
    const char * const location = location_array->p[i].c_str;
    ssize_t location_len = location_array->p[i].length;
    while (location_len > 0 && location[location_len - 1] == '/') {
      location_len--;
    }

    if (path_len < location_len) {
      continue;
    }

    if (path[location_len] != '/' && path_len > location_len) {
      continue;
    }

#if 0  // TODO(rbalint) enforce alignment of cstring_view_array entries to make this safe and quick
    /* Try comparing only the first 8 bytes to potentially save a call to memcmp */
    if (location_len >= sizeof(int64_t)
        && *(const int64_t*)location != *(const int64_t*)path) {
      /* Does not break the loop if path_ > location->name_ */
      // TODO(rbalint) maybe the loop could be broken making this function even faster
      continue;
    }
#endif

    const int memcmp_res = memcmp(location, path, location_len);
    if (memcmp_res < 0) {
      continue;
    } else if (memcmp_res > 0) {
      return false;
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

