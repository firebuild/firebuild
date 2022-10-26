/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
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
 * Remove environment variables injected by firebuild, to disable interception of children.
 */
void env_purge(char **env);

#endif  // FIREBUILD_ENV_H_
