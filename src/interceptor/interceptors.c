/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 */

/* interceptors.{cc,h} are the minimum necessary boilerplate
 * around the auto-generated gen_* interceptor files. */

#include "interceptor/interceptors.h"

#include <errno.h>

#include "common/firebuild_common.h"
#include "common/platform.h"
#include "interceptor/env.h"
#include "interceptor/ic_file_ops.h"
#include "interceptor/intercept.h"

void init_interceptors() {
/* Include the auto-generated initializations of the ic_orig function pointers */
#include "interceptor/gen_init.c"

  reset_interceptors();
}

void reset_interceptors() {
/* Include the auto-generated resetting of the internal states */
#include "interceptor/gen_reset.c"
}

/* Include the auto-generated definitions of the ic_orig function pointers */
#include "interceptor/gen_def.c"

/* Include the auto-generated implementations of the interceptor functions */
#include "interceptor/gen_impl.c"
