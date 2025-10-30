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

#ifndef FIREBUILD_UTILS_H_
#define FIREBUILD_UTILS_H_

#include <dirent.h>
#include <stdio.h>
#include <sys/types.h>

#include <string>

#include "common/platform.h"
#include "./fbbcomm.h"

/** Wrapper retrying on recoverable errors */
ssize_t fb_copy_file_range(int fd_in, loff_t *off_in, int fd_out, loff_t *off_out, size_t len,
                           unsigned int flags);

/** Add two struct timespecs. Equivalent to the same method of BSD. */
#define timespecadd(a, b, res) do {             \
  (res)->tv_sec = (a)->tv_sec + (b)->tv_sec;    \
  (res)->tv_nsec = (a)->tv_nsec + (b)->tv_nsec; \
  if ((res)->tv_nsec >= 1000 * 1000 * 1000) {   \
    (res)->tv_sec++;                            \
    (res)->tv_nsec -= 1000 * 1000 * 1000;       \
  }                                             \
} while (0)

/** Subtract two struct timespecs. Equivalent to the same method of BSD. */
#define timespecsub(a, b, res) do {             \
  (res)->tv_sec = (a)->tv_sec - (b)->tv_sec;    \
  (res)->tv_nsec = (a)->tv_nsec - (b)->tv_nsec; \
  if ((res)->tv_nsec < 0) {                     \
    (res)->tv_sec--;                            \
    (res)->tv_nsec += 1000 * 1000 * 1000;       \
  }                                             \
} while (0)

/** Compare two struct timespecs. Equivalent to the same method of BSD. */
#define timespeccmp(a, b, OP)                   \
  (((a)->tv_sec == (b)->tv_sec) ?               \
      (((a)->tv_nsec)OP((b)->tv_nsec)) :        \
       (((a)->tv_sec)OP((b)->tv_sec)))

/** Get fixed up dirent type even if the underlying filesystem did not support d_type. */
unsigned char fixed_dirent_type(const struct dirent* dirent, DIR* dir,
                                const std::string& dir_path);

/**
 * Return file size of dir / name in bytes, or 0 on error (printing error)
 * Directory sizes are reported as 0 bytes.
 */
off_t file_size(DIR* dir, const char* name);

/** Returns total size of all regular files in the directory recursively. */
off_t recursive_total_file_size(const std::string& path);

/** Overwrite file with the passed printf formatted string */
int file_overwrite_printf(const std::string& path, const char* format, ...);

/**
 * Bump RLIMIT_NOFILE to hard limit to allow more parallel interceptor connections.
 */
void bump_limits();

#ifndef __APPLE__
/** Check if a binary is statically linked */
bool is_statically_linked(const char *filename);
#endif
namespace firebuild {

/**
 * ACK a message from the supervised process
 * @param conn connection file descriptor to send the ACK on
 * @param ack_num the ACK id
 */
void ack_msg(const int conn, const uint16_t ack_num);
/**
 * Send an FBB message along with its header, potentially attaching two fds as ancillary data.
 *
 * These fds will appear in the intercepted process as opened file descriptors, possibly at
 * different numeric values (the numbers are automatically rewritten by the kernel).
 * This is sort of a cross-process dup(), see SCM_RIGHTS in cmsg(3) and unix(7).
 * Also see #656 for the overall design why we're doing this.
 *
 * If there are fds to attach, the message header and the message payload are sent in separate
 * steps, the message payload carrying the attached fds.
 *
 * @param conn connection file descriptor
 * @param ack_num the ack_num to send
 * @param msg the FBB message's builder object
 * @param fds pointer to the file descriptor array
 * @param fd_count number of fds to send
 */
void send_fbb(int conn, int ack_num, const FBBCOMM_Builder *msg, int *fds = NULL, int fd_count = 0);

void fb_perror(const char *s);

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1 << 0)
#endif
int fb_renameat2(int olddirfd, const char *oldpath,
                 int newdirfd, const char *newpath, unsigned int flags);

/**
 * Deduplicated strings allocated for the lifetime of the firebuild process.
 */
const std::string& deduplicated_string(std::string);

/**
 * Check if system configuration allows intercepting the build process.
 */
bool check_system_setup();

}  /* namespace firebuild */
#endif  // FIREBUILD_UTILS_H_
