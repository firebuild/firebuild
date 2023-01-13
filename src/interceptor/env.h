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

#ifndef FIREBUILD_ENV_H_
#define FIREBUILD_ENV_H_

#include <stdbool.h>

/**
 * Whether the environment needs any fixup at all.
 */
bool env_needs_fixup(char **env);

/**
 * Returns a size that's large enough to hold the fixed up environment,
 * including the array of pointers, and the strings that needed to be modified.
 *
 * This method was designed to be usable if the caller wants to fix the environment
 * on the stack, because exec*() need this.
 */
int get_env_fixup_size(char **env);

/**
 * Fixes up the environment to hold the essential stuff required for Firebuild.
 *
 * Wherever possible, only the pointers are copied. Wherever necessary, a copy and
 * fix of the string is created.
 *
 * buf has to be large enough to contain the data, as returned by get_env_fixup_size().
 *
 * The fixed up environment will begin at buf.
 *
 * This method was designed to be usable if the caller wants to fix the environment
 * on the stack, because exec*() need this.
 */
void env_fixup(char **env, void *buf);

/**
 * Fix up the environment
 *
 * This is racy because it operates on the global "environ", but is probably good enough.
 * A proper solution would require to prefix "cmd" with a wrapper that fixes it up, but that could
 * be slow. */
#define ENVIRON_SAVE_AND_FIXUP(did_env_fixup, environ_saved)    \
  bool did_env_fixup = false;                                   \
  char **environ_saved = environ;                               \
  if (env_needs_fixup(environ)) {                               \
    did_env_fixup = true;                                       \
    int env_fixup_size = get_env_fixup_size(environ);           \
    environ = alloca(env_fixup_size);                           \
    env_fixup(environ_saved, environ);                          \
  }                                                             \
  do {                                                          \
  } while (0)

#define ENVIRON_RESTORE(did_env_fixup, environ_saved)   \
  if (did_env_fixup) {                                  \
    environ = environ_saved;                            \
  }                                                     \
  do {                                                  \
  } while (0)

/**
 * Remove environment variables injected by firebuild, to disable interception of children.
 */
void env_purge(char **env);

#endif  // FIREBUILD_ENV_H_
