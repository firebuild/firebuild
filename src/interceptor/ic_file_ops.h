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

#ifndef FIREBUILD_IC_FILE_OPS_H_
#define FIREBUILD_IC_FILE_OPS_H_

#include <link.h>
#include <dirent.h>
#include <stdio.h>

#include "interceptor/intercept.h"
#include "interceptor/interceptors.h"

int intercept_fopen_mode_to_open_flags_helper(const char * mode);
int popen_type_to_flags(const char * type);
void clear_notify_on_read_write_state(const int fd);
void set_notify_on_read_write_state(const int fd);
void set_all_notify_on_read_write_states();
void copy_notify_on_read_write_state(const int to_fd, const int from_fd);

/* Same as fileno(), but with safe NULL pointer handling. */
static inline int safe_fileno(FILE *stream) {
  int ret = stream ? get_ic_orig_fileno()(stream) : -1;
  if (ret == fb_sv_conn) {
    assert(0 && "fileno() returned the connection fd");
  }
  return ret;
}

/* Same as dirfd(), but with safe NULL pointer handling. */
static inline int safe_dirfd(DIR *dirp) {
  int ret = dirp ? get_ic_orig_dirfd()(dirp) : -1;
  if (ret == fb_sv_conn) {
    assert(0 && "dirfd() returned the connection fd");
  }
  return ret;
}

#endif  // FIREBUILD_IC_FILE_OPS_H_
