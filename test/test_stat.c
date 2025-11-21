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

 /*
 * Test stat family syscalls: stat, lstat, fstat, fstatat, statx,
 * and glibc-internal __xstat variants.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __linux__
/* For statx if available */
#ifndef STATX_TYPE
#include <linux/stat.h>
#endif
#endif

#define _STAT_VER 1

static void fail(const char *msg) {
  perror(msg);
  exit(1);
}

int main(void) {
  struct stat st;
  /* Create test files */
  const char *regular = "test_stat_regular";
  int fd = creat(regular, 0644);
  if (fd == -1) fail("creat regular");
  if (write(fd, "test", 4) != 4) fail("write");
  close(fd);

  const char *symlink_name = "test_stat_symlink";
  if (symlink(regular, symlink_name) != 0) fail("symlink");

  /* Test standard POSIX stat family */
  if (stat(regular, &st) != 0) {
    fail("stat");
  }
  if (!S_ISREG(st.st_mode)) {
    fprintf(stderr, "stat: not regular\n");
    exit(1);
  }
  if (st.st_size != 4) {
    fprintf(stderr, "stat: size mismatch\n");
    exit(1);
  }

  if (lstat(symlink_name, &st) != 0) {
    fail("lstat");
  }
  if (!S_ISLNK(st.st_mode)) {
    fprintf(stderr, "lstat: should return symlink\n");
    exit(1);
  }

  fd = open(regular, O_RDONLY);
  if (fd == -1) {
    fail("open for fstat");
  }
  if (fstat(fd, &st) != 0) {
    fail("fstat");
  }
  if (!S_ISREG(st.st_mode)) {
    fprintf(stderr, "fstat: not regular\n");
    exit(1);
  }
  close(fd);

  if (fstatat(AT_FDCWD, regular, &st, 0) != 0) {
    fail("fstatat");
  }
  if (!S_ISREG(st.st_mode)) {
    fprintf(stderr, "fstatat: not regular\n");
    exit(1);
  }

  if (fstatat(AT_FDCWD, symlink_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
    fail("fstatat AT_SYMLINK_NOFOLLOW");
  }
  if (!S_ISLNK(st.st_mode)) {
    fprintf(stderr, "fstatat AT_SYMLINK_NOFOLLOW: should be symlink\n");
    exit(1);
  }

#ifdef __x86_64__
  /* Test 64-bit standard variants */
  struct stat64 st64;
  if (stat64(regular, &st64) != 0) {
    fail("stat64");
  }
  if (!S_ISREG(st64.st_mode)) {
    fprintf(stderr, "stat64: not regular\n");
    exit(1);
  }

  if (lstat64(symlink_name, &st64) != 0) {
    fail("lstat64");
  }
  if (!S_ISLNK(st64.st_mode)) {
    fprintf(stderr, "lstat64: should be symlink\n");
    exit(1);
  }

  fd = open(regular, O_RDONLY);
  if (fd == -1) {
    fail("open for fstat64");
  }
  if (fstat64(fd, &st64) != 0) {
    fail("fstat64");
  }
  if (!S_ISREG(st64.st_mode)) {
    fprintf(stderr, "fstat64: not regular\n");
    exit(1);
  }
  close(fd);

  if (fstatat64(AT_FDCWD, regular, &st64, 0) != 0) {
    fail("fstatat64");
  }
  if (!S_ISREG(st64.st_mode)) {
    fprintf(stderr, "fstatat64: not regular\n");
    exit(1);
  }
#endif

#ifdef STATX_TYPE
  /* Test statx (Linux-specific, kernel 4.11+) */
  struct statx stx;
  if (statx(AT_FDCWD, regular, 0, STATX_BASIC_STATS, &stx) == 0) {
    if (!S_ISREG(stx.stx_mode)) {
      fprintf(stderr, "statx: not regular\n");
      exit(1);
    }
    if (stx.stx_size != 4) {
      fprintf(stderr, "statx: size mismatch\n");
      exit(1);
    }
  } else if (errno != ENOSYS) {
    fail("statx");
  }

  if (statx(AT_FDCWD, symlink_name, AT_SYMLINK_NOFOLLOW,
            STATX_BASIC_STATS, &stx) == 0) {
    if (!S_ISLNK(stx.stx_mode)) {
      fprintf(stderr, "statx AT_SYMLINK_NOFOLLOW: should be symlink\n");
      exit(1);
    }
  } else if (errno != ENOSYS) {
    fail("statx AT_SYMLINK_NOFOLLOW");
  }
#endif

  /* Cleanup */
  unlink(symlink_name);
  unlink(regular);

  printf("ok\n");
  return 0;
}
