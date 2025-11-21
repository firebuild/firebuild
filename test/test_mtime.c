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
 * Test mtime-setting syscalls: utime, utimes, utimensat, futimens including UTIME_NOW.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

static void fail(const char *msg) {
  perror(msg);
  exit(1);
}

static void check_close_to_now(const struct stat *st, const char *ctx) {
  struct timespec now;
  if (clock_gettime(CLOCK_REALTIME, &now) != 0) fail("clock_gettime");
  /* Allow a generous 5s window. */
  if (!(st->st_mtime >= now.tv_sec - 5 && st->st_mtime <= now.tv_sec + 5)) {
    fprintf(stderr, "%s: mtime %" PRId64 " not close to now %" PRId64 "\n",
            ctx, (int64_t)st->st_mtime, (int64_t)now.tv_sec);
    exit(1);
  }
}

int main(void) {
  struct stat st;
  int fd;

  /* utime with explicit times */
  const char *f1 = "mtime1";
  int f1fd = creat(f1, 0600);
  if (f1fd == -1) fail("creat mtime1");
  close(f1fd);
  struct utimbuf ub; ub.actime = 1600000000; ub.modtime = 1600000100;
  if (utime(f1, &ub) != 0) fail("utime explicit");
  if (stat(f1, &st) != 0) fail("stat mtime1");
  if (st.st_mtime != 1600000100) {
    fprintf(stderr, "utime explicit mismatch %" PRId64 "\n",
            (int64_t)st.st_mtime);
    exit(1);
  }  /* utime with NULL (current time) */
  if (utime(f1, NULL) != 0) fail("utime NULL");
  if (stat(f1, &st) != 0) fail("stat mtime1 after NULL");
  check_close_to_now(&st, "utime NULL");

  /* utimes with explicit timeval */
  const char *f2 = "mtime2";
  int f2fd = creat(f2, 0600);
  if (f2fd == -1) {
    fail("creat mtime2");
    close(f2fd);
  }
  struct timeval tv[2];
  tv[0].tv_sec = 1600000200; tv[0].tv_usec = 500000;
  tv[1].tv_sec = 1600000300; tv[1].tv_usec = 700000;
  if (utimes(f2, tv) != 0) fail("utimes explicit");
  if (stat(f2, &st) != 0) fail("stat mtime2");
  if (st.st_mtime != 1600000300) {
    fprintf(stderr, "utimes explicit mismatch %" PRId64 "\n",
            (int64_t)st.st_mtime);
    exit(1);
  }

  /* utimensat with explicit timespec */
  const char *f3 = "mtime3";
  int f3fd = creat(f3, 0600);
  if (f3fd == -1) {
    fail("creat mtime3");
  }
  close(f3fd);
  struct timespec ts[2];
  ts[0].tv_sec = 1600000400; ts[0].tv_nsec = 900000000;
  ts[1].tv_sec = 1600000500; ts[1].tv_nsec = 123456789;
  if (utimensat(AT_FDCWD, f3, ts, 0) != 0) fail("utimensat explicit");
  if (stat(f3, &st) != 0) fail("stat mtime3");
  if (st.st_mtime != 1600000500) {
    fprintf(stderr, "utimensat explicit mtime mismatch %" PRId64 "\n",
            (int64_t)st.st_mtime);
    exit(1);
  }

  /* utimensat UTIME_NOW */
  const char *f4 = "mtime4";
  int f4fd = creat(f4, 0600);
  if (f4fd == -1) {
    fail("creat mtime4");
  }
  close(f4fd);
  ts[0].tv_sec = 0; ts[0].tv_nsec = UTIME_NOW;
  ts[1].tv_sec = 0; ts[1].tv_nsec = UTIME_NOW;
  if (utimensat(AT_FDCWD, f4, ts, 0) != 0) fail("utimensat UTIME_NOW");
  if (stat(f4, &st) != 0) fail("stat mtime4");
  check_close_to_now(&st, "utimensat UTIME_NOW");

  /* futimens explicit */
  const char *f5 = "mtime5";
  fd = creat(f5, 0600);
  if (fd == -1) {
    fail("creat mtime5");
  }
  ts[0].tv_sec = 1600000600; ts[0].tv_nsec = 888000000;
  ts[1].tv_sec = 1600000700; ts[1].tv_nsec = 111000000;
  if (futimens(fd, ts) != 0) fail("futimens explicit");
  if (fstat(fd, &st) != 0) fail("fstat mtime5 explicit");
  if (st.st_mtime != 1600000700) {
    fprintf(stderr, "futimens explicit mtime mismatch %" PRId64 "\n",
            (int64_t)st.st_mtime);
    exit(1);
  }
  close(fd);

  /* futimens UTIME_NOW */
  fd = open(f5, O_RDWR);
  if (fd == -1) {
    fail("open mtime5");
  }
  ts[0].tv_sec = 0; ts[0].tv_nsec = UTIME_NOW;
  ts[1].tv_sec = 0; ts[1].tv_nsec = UTIME_NOW;
  if (futimens(fd, ts) != 0) fail("futimens UTIME_NOW");
  if (fstat(fd, &st) != 0) fail("fstat mtime5 UTIME_NOW");
  check_close_to_now(&st, "futimens UTIME_NOW");
  close(fd);

  /* Cleanup */
  unlink(f1); unlink(f2); unlink(f3); unlink(f4); unlink(f5);

  printf("ok\n");
  return 0;
}
