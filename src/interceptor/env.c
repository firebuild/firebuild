/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */


#include "interceptor/env.h"

#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interceptor/intercept.h"


/* Avoid typos in repetitive names */
#define FB_INSERT_TRACE_MARKERS "FB_INSERT_TRACE_MARKERS"
#define FB_SOCKET               "FB_SOCKET"
#define FB_SEMAPHORE            "FB_SEMAPHORE"
#define LD_LIBRARY_PATH         "LD_LIBRARY_PATH"
#define LD_PRELOAD              "LD_PRELOAD"
#define LIBFBINTERCEPT_SO       "libfbintercept.so"
#define LIBFBINTERCEPT_SO_LEN   ((int) strlen(LIBFBINTERCEPT_SO))

void get_argv_env(char *** argv, char ***env) {
  char* arg = *(__environ - 2);
  unsigned long int argc_guess = 0;

  /* argv is NULL terminated */
  assert(*(__environ - 1) == NULL);
  /* walk back on argv[] to find the first value matching the counted argument number */
  while (argc_guess != (unsigned long int)arg) {
    argc_guess++;
    arg = *(__environ - 2 - argc_guess);
  }

  *argv = __environ - 1 - argc_guess;
  *env = __environ;
}

// TODO(rbalint) for valgrind
// void free_arc_argv_env()


/* Like getenv(), but from a custom environment array */
static char *getenv_from(char **env, const char *name) {
  int name_len = strlen(name);
  for (int i = 0; env[i] != NULL; i++) {
    if (memcmp(name, env[i], name_len) == 0 && env[i][name_len] == '=') {
      return env[i] + name_len + 1;
    }
  }
  return NULL;
}

static bool fb_insert_trace_markers_needs_fixup(char **env) {
  char *current_value = getenv_from(env, FB_INSERT_TRACE_MARKERS);
  if (current_value == NULL && !insert_trace_markers) {
    return false;
  }
  if (current_value == NULL || !insert_trace_markers) {
    return true;
  }
  return strcmp(current_value, "1") != 0;
}

static bool fb_socket_needs_fixup(char **env) {
  char *current_value = getenv_from(env, FB_SOCKET);
  return current_value == NULL || strcmp(current_value, fb_conn_string) != 0;
}

static bool fb_semaphore_needs_fixup(char **env) {
  char *current_value = getenv_from(env, FB_SEMAPHORE);
  return current_value == NULL || strcmp(current_value, fb_sema_string) != 0;
}

static bool ld_library_path_needs_fixup(char **env) {
  if (env_ld_library_path == NULL) {
    return false;
  }
  char *current_value = getenv_from(env, LD_LIBRARY_PATH);

  // FIXME use more precise search (split both values at ':' or ';', and compare the components)
  return current_value == NULL || strstr(current_value, env_ld_library_path) == NULL;
}

static bool ld_preload_needs_fixup(char **env) {
  char *current_value = getenv_from(env, LD_PRELOAD);
  if (current_value == NULL) {
    return true;
  }

  const char *loc = strstr(current_value, LIBFBINTERCEPT_SO);
  if (loc) {
    const char *loc_end = loc + LIBFBINTERCEPT_SO_LEN;
    if ((loc == current_value || *(loc - 1) == ':' || *(loc - 1) == ' ')
        && (*loc_end == '\0' || *loc_end == ':' || *loc_end == ' ')) {
      return false;
    }
  }

  return true;
}

bool env_needs_fixup(char **env) {
  /* FB_SYSTEM_LOCATIONS is not fixed up because it is not needed for correctness, just for
   * improving performance a bit. */
  return intercepting_enabled &&
      (fb_insert_trace_markers_needs_fixup(env) ||
       fb_socket_needs_fixup(env) ||
       ld_library_path_needs_fixup(env) ||
       ld_preload_needs_fixup(env));
}

int get_env_fixup_size(char **env) {
  size_t ret;

  /* Count the number of items. */
  int i;
  for (i = 0; env[i] != NULL; i++) {}
  /* At most 4 env vars might need to be freshly created, plus make room for the trailing NULL. */
  ret = (i + 5) * sizeof(char *);

  /* Room required, depending on the variable:
     - variable name + equals sign + restored value + trailing NUL, or
     - variable name + equals sign + current value + separator + restored value appended + trailing NUL.
     We might be counting a slightly upper estimate. */
  ret += strlen(FB_INSERT_TRACE_MARKERS "=1") + 1;
  ret += strlen(FB_SOCKET "=") + strlen(fb_conn_string) + 1;
  ret += strlen(FB_SEMAPHORE "=") + strlen(fb_sema_string) + 1;

  char *e = getenv_from(env, LD_PRELOAD);
  ret += strlen(LD_PRELOAD "=") + (e ? strlen(e) : 0) + 1 + LIBFBINTERCEPT_SO_LEN + 1;

  e = getenv_from(env, LD_LIBRARY_PATH);
  ret += strlen(LD_LIBRARY_PATH "=") +
      (e ? strlen(e) : 0) + 1 +
      (env_ld_library_path ? strlen(env_ld_library_path) : 0) + 1;

  return ret;
}

/* Places the desired value of the FB_INSERT_TRACE_MARKERS env var
 * (including the "FB_INSERT_TRACE_MARKERS=" prefix) at @p.
 *
 * Returns the number of bytes placed (including the trailing NUL),
 * or 0 if this variable doesn't need to be set.
 */
static int fixup_fb_insert_trace_markers(char *p) {
  insert_debug_msg("Fixing up FB_INSERT_TRACE_MARKERS in the environment");
  if (!insert_trace_markers) {
    return 0;
  }
  int offset;
  sprintf(p, "%s=1%n", FB_INSERT_TRACE_MARKERS, &offset);  /* NOLINT */
  return offset + 1;
}

/* Places the desired value of the FB_SOCKET env var
 * (including the "FB_SOCKET=" prefix) at @p.
 *
 * Returns the number of bytes placed (including the trailing NUL).
 */
static int fixup_fb_socket(char *p) {
  insert_debug_msg("Fixing up FB_SOCKET in the environment");
  int offset;
  sprintf(p, "%s=%s%n", FB_SOCKET, fb_conn_string, &offset);  /* NOLINT */
  return offset + 1;
}

/* Places the desired value of the FB_SEMAPHORE env var
 * (including the "FB_SEMAPHORE=" prefix) at @p.
 *
 * Returns the number of bytes placed (including the trailing NUL).
 */
static int fixup_fb_semaphore(char *p) {
  insert_debug_msg("Fixing up FB_SEMAPHORE in the environment");
  int offset;
  sprintf(p, "%s=%s%n", FB_SEMAPHORE, fb_sema_string, &offset);  /* NOLINT */
  return offset + 1;
}

/* Places the desired value of the LD_LIBRARY_PATH env var
 * (including the "LD_LIBRARY_PATH=" prefix) at @p.
 * The desired value depends on the @current_value.
 *
 * Returns the number of bytes placed (including the trailing NUL).
 */
static int fixup_ld_library_path(const char *current_value, char *p) {
  insert_debug_msg("Fixing up LD_LIBRARY_PATH in the environment");
  int offset;
  if (current_value == NULL) {
    sprintf(p, "%s=%s%n", LD_LIBRARY_PATH, env_ld_library_path, &offset);  /* NOLINT */
  } else {
    sprintf(p, "%s=%s:%s%n", LD_LIBRARY_PATH, current_value, env_ld_library_path, &offset);  /* NOLINT */
  }
  return offset + 1;
}

/* Places the desired value of the LD_PRELOAD env var
 * (including the "LD_PRELOAD=" prefix) at @p.
 * The desired value depends on the @current_value.
 *
 * Appends libfbintercept.so to the end, if needed.
 * (The intercepted program removed libfbintercept.so from LD_PRELOAD and added something,
 * presumably its own library instead of _prepending_ its own library. The fix is thus _appending_
 * libfbintercept.so to pretend that the program did the proper prepending.)
 *
 * Returns the number of bytes placed (including the trailing NUL).
 */
static int fixup_ld_preload(const char *current_value, char *p) {
  insert_debug_msg("Fixing up LD_PRELOAD in the environment");
  int offset;
  if (current_value == NULL) {
    sprintf(p, "%s=%s%n", LD_PRELOAD, LIBFBINTERCEPT_SO, &offset);  /* NOLINT */
  } else {
    /* Append the library. */
    sprintf(p, "%s=%s:%s%n", LD_PRELOAD, current_value, LIBFBINTERCEPT_SO, &offset);  /* NOLINT */
  }
  return offset + 1;
}

static inline bool begins_with(const char *str, const char *prefix) {
  return strncmp(str, prefix, strlen(prefix)) == 0;
}

void env_fixup(char **env, void *buf) {
  /* The first part of buf, accessed via buf1, up to buf2, will contain the new env pointers.
   * The second part, from buf2, will contain the env strings that needed to be copied and fixed. */
  char **buf1 = (char **) buf;
  assert(buf1 != NULL);  /* Make scan-build happy */

  /* Count the number of items. */
  int i;
  for (i = 0; env[i] != NULL; i++) {}
  /* At most 4 env vars might need to be freshly created, plus make room for the trailing NULL. */
  char *buf2 = (char *) buf + (i + 5) * sizeof(char *);

  bool fb_insert_trace_markers_fixed_up = false;
  bool fb_socket_fixed_up = false;
  bool fb_semaphore_fixed_up = false;
  bool ld_library_path_fixed_up = false;
  bool ld_preload_fixed_up = false;

  /* Fix up FB_INSERT_TRACE_MARKERS if needed */
  if (fb_insert_trace_markers_needs_fixup(env)) {
    int size = fixup_fb_insert_trace_markers(buf2);
    if (size > 0) {
      *buf1++ = buf2;
      buf2 += size;
    }
    fb_insert_trace_markers_fixed_up = true;
  }

  /* Fix up FB_SOCKET if needed */
  if (fb_socket_needs_fixup(env)) {
    int size = fixup_fb_socket(buf2);
    assert(size > 0);
    *buf1++ = buf2;
    buf2 += size;
    fb_socket_fixed_up = true;
  }

  /* Fix up FB_SEMAPHORE if needed */
  if (fb_semaphore_needs_fixup(env)) {
    int size = fixup_fb_semaphore(buf2);
    assert(size > 0);
    *buf1++ = buf2;
    buf2 += size;
    fb_semaphore_fixed_up = true;
  }

  /* Fix up LD_LIBRARY_PATH if needed */
  if (ld_library_path_needs_fixup(env)) {
    char *current_value = getenv_from(env, LD_LIBRARY_PATH);
    int size = fixup_ld_library_path(current_value, buf2);
    assert(size > 0);
    *buf1++ = buf2;
    buf2 += size;
    ld_library_path_fixed_up = true;
  }

  /* Fix up LD_PRELOAD if needed */
  if (ld_preload_needs_fixup(env)) {
    char *current_value = getenv_from(env, LD_PRELOAD);
#ifndef NDEBUG
    int size =
#endif
        fixup_ld_preload(current_value, buf2);
    assert(size > 0);
    *buf1++ = buf2;
    /* buf2 += size; */
    ld_preload_fixed_up = true;
  }

  /* Copy the environment, skipping the ones that we already fixed up. */
  for (i = 0; env[i] != NULL; i++) {
    if ((fb_insert_trace_markers_fixed_up && begins_with(env[i], FB_INSERT_TRACE_MARKERS "=")) ||
        (fb_socket_fixed_up && begins_with(env[i], FB_SOCKET "=")) ||
        (fb_semaphore_fixed_up && begins_with(env[i], FB_SEMAPHORE "=")) ||
        (ld_library_path_fixed_up && begins_with(env[i], LD_LIBRARY_PATH "=")) ||
        (ld_preload_fixed_up && begins_with(env[i], LD_PRELOAD "="))) {
      continue;
    }
    *buf1++ = env[i];
  }

  *buf1 = NULL;
}

void env_purge(char **env) {
  char **cur = env;
  assert(cur != NULL);  /* Make scan-build happy */

  /* Copy the environment, skipping the ones that need to be removed. */
  for (int i = 0; env[i] != NULL; i++) {
    if ((begins_with(env[i], FB_INSERT_TRACE_MARKERS "=")) ||
        (begins_with(env[i], FB_SOCKET "="))) {
      continue;
    }
    if (begins_with(env[i], LD_PRELOAD "=")) {
      /* Clear libfbintercept.so */
      if (strcmp(env[i], LD_PRELOAD "=" LIBFBINTERCEPT_SO) == 0) {
        /* Just skip LD_PRELOAD. */
        continue;
      } else {
        char * start = strstr(env[i], LIBFBINTERCEPT_SO);
        size_t move_len = LIBFBINTERCEPT_SO_LEN;
        if (start) {
          if (*(start - 1) == ':' || *(start - 1) == ' ') {
            /* Clear separator before. */
            start--; move_len++;
          } else if (*(start + move_len) == ':' || *(start + move_len) == ' ') {
            /* Clear separator after. */
            move_len++;
          }
          size_t remaining_len = strlen(start);
          /* Move LD_PRELOAD's contents including '\0' earlier to overwrite libfbintercept.so. */
          memmove(start, start + move_len,
                  remaining_len - move_len + 1);
        }
      }
    }
    *cur++ = env[i];
  }

  *cur = NULL;
}
