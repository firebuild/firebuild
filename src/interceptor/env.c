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


#include "interceptor/env.h"

#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/config.h"
#include "interceptor/intercept.h"


/* Avoid typos in repetitive names */
#define FB_INSERT_TRACE_MARKERS "FB_INSERT_TRACE_MARKERS"
#define FB_SOCKET               "FB_SOCKET"

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
#ifdef FB_EXTRA_DEBUG
  char *current_value = getenv_from(env, FB_INSERT_TRACE_MARKERS);
  if (current_value == NULL && !insert_trace_markers) {
    return false;
  }
  if (current_value == NULL || !insert_trace_markers) {
    return true;
  }
  return strcmp(current_value, "1") != 0;
#else
  (void)env;
  return false;
#endif
}

static bool fb_socket_needs_fixup(char **env) {
  char *current_value = getenv_from(env, FB_SOCKET);
  return current_value == NULL || strcmp(current_value, fb_conn_string) != 0;
}

static bool ld_preload_needs_fixup(char **env) {
  char *current_value = getenv_from(env, LD_PRELOAD);
  if (current_value == NULL) {
    return true;
  }

  const char *loc = strstr(current_value, libfirebuild_so);
  if (loc) {
    const char *loc_end = loc + libfirebuild_so_len;
    if ((loc == current_value || *(loc - 1) == ':' || *(loc - 1) == ' ')
        && (*loc_end == '\0' || *loc_end == ':' || *loc_end == ' ')) {
      return false;
    }
  }

  return true;
}

bool env_needs_fixup(char **env) {
  /* FB_READ_ONLY_LOCATIONS and FB_IGNORE_LOCATIONS are not fixed up because they are not needed
   * for correctness, just for improving performance a bit. */
  return (fb_insert_trace_markers_needs_fixup(env) ||
          fb_socket_needs_fixup(env) ||
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
#ifdef FB_EXTRA_DEBUG
  ret += strlen(FB_INSERT_TRACE_MARKERS "=1") + 1;
#endif
  ret += strlen(FB_SOCKET "=") + strlen(fb_conn_string) + 1;

  char *e = getenv_from(env, LD_PRELOAD);
  ret += strlen(LD_PRELOAD "=") + (e ? strlen(e) : 0) + 1 + libfirebuild_so_len + 1;

  return ret;
}

#ifdef FB_EXTRA_DEBUG
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
#endif

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

/* Places the desired value of the LD_PRELOAD env var
 * (including the "LD_PRELOAD=" prefix) at @p.
 * The desired value depends on the @current_value.
 *
 * Appends libfirebuild.so to the end, if needed.
 * (The intercepted program removed libfirebuild.so from LD_PRELOAD and added something,
 * presumably its own library instead of _prepending_ its own library. The fix is thus _appending_
 * libfirebuild.so to pretend that the program did the proper prepending.)
 *
 * Returns the number of bytes placed (including the trailing NUL).
 */
static int fixup_ld_preload(const char *current_value, char *p) {
  insert_debug_msg("Fixing up LD_PRELOAD in the environment");
  int offset;
  if (current_value == NULL) {
    sprintf(p, "%s=%s%n", LD_PRELOAD, libfirebuild_so, &offset);  /* NOLINT */
  } else {
    /* Append the library. */
    sprintf(p, "%s=%s:%s%n", LD_PRELOAD, current_value, libfirebuild_so, &offset);  /* NOLINT */
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
  bool ld_preload_fixed_up = false;

#ifdef FB_EXTRA_DEBUG
  /* Fix up FB_INSERT_TRACE_MARKERS if needed */
  if (fb_insert_trace_markers_needs_fixup(env)) {
    int size = fixup_fb_insert_trace_markers(buf2);
    if (size > 0) {
      *buf1++ = buf2;
      buf2 += size;
    }
    fb_insert_trace_markers_fixed_up = true;
  }
#endif

  /* Fix up FB_SOCKET if needed */
  if (fb_socket_needs_fixup(env)) {
    int size = fixup_fb_socket(buf2);
    assert(size > 0);
    *buf1++ = buf2;
    buf2 += size;
    fb_socket_fixed_up = true;
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
        (ld_preload_fixed_up && begins_with(env[i], LD_PRELOAD "="))) {
      continue;
    }
    *buf1++ = env[i];
  }

  qsort(buf, buf1 - (char**)buf, sizeof(char**), cmpstringpp);
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
      /* Clear libfirebuild.so */
      if (strcmp(env[i] + strlen(LD_PRELOAD "="), libfirebuild_so) == 0) {
        /* Just skip LD_PRELOAD. */
        continue;
      } else {
        char * start = strstr(env[i], libfirebuild_so);
        size_t move_len = libfirebuild_so_len;
        if (start) {
          if (*(start - 1) == ':' || *(start - 1) == ' ') {
            /* Clear separator before. */
            start--; move_len++;
          } else if (*(start + move_len) == ':' || *(start + move_len) == ' ') {
            /* Clear separator after. */
            move_len++;
          }
          size_t remaining_len = strlen(start);
          /* Move LD_PRELOAD's contents including '\0' earlier to overwrite libfirebuild.so. */
          memmove(start, start + move_len,
                  remaining_len - move_len + 1);
        }
      }
    }
    *cur++ = env[i];
  }

  *cur = NULL;
}
