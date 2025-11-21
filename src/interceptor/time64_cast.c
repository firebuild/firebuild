/*
 * Copyright (c) 2025 Interri Kft.
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

/** C utility functions to cast time values from 64-bit representations. */

#if (__SIZEOF_POINTER__ == 4)
#define __USE_TIME_BITS64 1
#define __USE_FILE_OFFSET_BITS 64
#endif

#define _GNU_SOURCE 1

#include "time64_cast.h"

#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <utime.h>

#include "common/platform.h"

int64_t stat64_mtim_sec_to_int64(const void* stat64_ptr) {
  return (int64_t)(((struct stat *)stat64_ptr)->st_mtim.tv_sec);
}

int64_t stat64_mtim_nsec_to_int64(const void* stat64_ptr) {
  return (int64_t)(((struct stat *)stat64_ptr)->st_mtim.tv_nsec);
}

#ifdef __linux__
int64_t statx_mtim_sec_to_int64(const void* statx_ptr) {
  return (int64_t)(((struct statx *)statx_ptr)->stx_mtime.tv_sec);
}

int64_t statx_mtim_nsec_to_int64(const void* statx_ptr) {
  return (int64_t)(((struct statx *)statx_ptr)->stx_mtime.tv_nsec);
}
#endif  // __linux__

int64_t timespec_array_mtim_sec_to_int64(const void* timespec_ptr) {
  return (int64_t)(((struct timespec *)timespec_ptr)[1].tv_sec);
}

int64_t timespec_array_mtim_nsec_to_int64(const void* timespec_ptr) {
  return (int64_t)(((struct timespec *)timespec_ptr)[1].tv_nsec);
}

int64_t timeval_array_mtim_sec_to_int64(const void* timeval_ptr) {
  return (int64_t)(((struct timeval *)timeval_ptr)[1].tv_sec);
}

int64_t timeval_array_mtim_usec_to_int64_nsec(const void* timeval_ptr) {
  return (int64_t)(((struct timeval *)timeval_ptr)[1].tv_usec) * 1000;
}

int64_t utimbuf_mtim_sec_to_int64(const void* utimbuf_ptr) {
  return (int64_t)(((struct utimbuf *)utimbuf_ptr)->modtime);
}
