/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_ENV_H_
#define FIREBUILD_ENV_H_

#ifdef  __cplusplus
extern "C" {
#endif

/**
 * Get process's arguments and environment
 */
void get_argv_env(char *** argv, char ***env);

#ifdef  __cplusplus
}
#endif

#endif  // FIREBUILD_ENV_H_
