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
/* Include the auto-generated initializations of the get_ic_orig function pointers */
  reset_interceptors();
}

void reset_interceptors() {
/* Include the auto-generated resetting of the internal states */
#include "interceptor/gen_reset.c"
}

/* Include the auto-generated definitions of the get_ic_orig function pointers */
#include "interceptor/gen_def.c"

/* Include the auto-generated implementations of the interceptor functions */
#include "interceptor/gen_impl.c"
