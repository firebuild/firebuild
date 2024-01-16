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

#ifndef COMMON_DEBUG_SYSFLAGS_H_
#define COMMON_DEBUG_SYSFLAGS_H_

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void debug_open_flags(FILE *f, int flags);
void debug_at_flags(FILE *f, int flags);
void debug_psfa_attr_flags(FILE *f, int flags);
void debug_fcntl_cmd(FILE *f, int cmd);
void debug_fcntl_arg_or_ret(FILE *f, int cmd, int arg);
void debug_socket_domain(FILE *f, int domain);
void debug_error_no(FILE *f, int error_no);
void debug_signum(FILE *f, int signum);
void debug_mode_t(FILE *f, mode_t mode);
void debug_wstatus(FILE *f, int wstatus);
void debug_clone_flags(FILE *f, int flags);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* COMMON_DEBUG_SYSFLAGS_H_ */
