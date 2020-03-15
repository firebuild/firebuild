/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

/* interceptors.{cc,h} are the minimum necessary boilerplate
 * around the auto-generated gen_* interceptor files. */

#include "interceptor/interceptors.h"

#include <errno.h>

#include "interceptor/ic_file_ops.h"
#include "interceptor/intercept.h"
#include "firebuild/platform.h"

namespace firebuild {

extern bool insert_trace_markers;

#ifdef  __cplusplus
extern "C" {
#endif

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

#ifdef  __cplusplus
}  // extern "C"
#endif

}  // namespace firebuild
