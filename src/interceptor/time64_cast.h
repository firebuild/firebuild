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

/** Utility functions to cast time values from 64-bit representations. */

#ifndef FIREBUILD_TIME64_CAST_H_
#define FIREBUILD_TIME64_CAST_H_

#include <time.h>
#include <stdint.h>

int64_t stat64_mtim_sec_to_int64(const void* stat64_ptr);
int64_t stat64_mtim_nsec_to_int64(const void* stat64_ptr);
#ifdef __linux__
int64_t statx_mtim_sec_to_int64(const void* statx_ptr);
int64_t statx_mtim_nsec_to_int64(const void* statx_ptr);
#endif  // __linux__
int64_t timespec_array_mtim_sec_to_int64(const void* timespec_ptr);
int64_t timespec_array_mtim_nsec_to_int64(const void* timespec_ptr);
int64_t timeval_array_mtim_sec_to_int64(const void* timeval_ptr);
int64_t timeval_array_mtim_usec_to_int64_nsec(const void* timeval_ptr);
int64_t utimbuf_mtim_sec_to_int64(const void* utimbuf_ptr);

#endif  // FIREBUILD_TIME64_CAST_H_
