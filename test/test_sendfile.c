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

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef __APPLE__
#include <sys/socket.h>
#include <sys/uio.h>
#else
#include <sys/sendfile.h>
#endif
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#define TOSTR(x) TOSTR2(x)
#define TOSTR2(x) #x
#define LOC "[" __FILE__ ":" TOSTR(__LINE__) "]"

int main() {
  int fd1, fd2;
#ifdef O_TMPFILE
  fd1 = open(".", O_RDWR | O_TMPFILE, 0644);
  if (fd1 == -1) {
    if (errno == ENOTSUP || errno == EISDIR) {
      return 0;
    } else {
      perror("open" LOC);
      exit(1);
    }
  }
#else
  /* sendfile() will fail, but that still excercises most of the code. */
  // TODO(rbalint) create socket and test with that
  fd1 = 0;
#endif
  fd2 = open("integration.bats", O_RDWR);
  if (fd2 == -1) {
    perror("open" LOC);
    close(fd1);
    exit(1);
  }

#ifdef __APPLE__
  off_t len = 10;
  if (sendfile(fd2, fd1, 0, &len, NULL, 0) == -1) {
#else
  if (sendfile(fd1, fd2, NULL, 10) == -1) {
#endif
    perror("sendfile" LOC);
    close(fd1);
    close(fd2);
    exit(1);
  }

  if (syscall(SYS_sendfile, fd1, fd2, NULL, 10) == -1) {
    perror("SYS_sendfile" LOC);
    close(fd1);
    close(fd2);
    exit(1);
  }

#ifndef __APPLE__
  if (copy_file_range(fd2, NULL, fd1, NULL, 10, 0) == -1) {
    perror("copy_file_range" LOC);
    close(fd1);
    close(fd2);
    exit(1);
  }

  /* test inherited fds*/
  off_t offset = 0;
  if (copy_file_range(0, NULL, 1, &offset, 10, 0) == -1) {
    perror("copy_file_range" LOC);
    close(fd1);
    close(fd2);
    exit(1);
  }
  if (copy_file_range(0, &offset, 1, NULL, 10, 0) == -1) {
    perror("copy_file_range" LOC);
    close(fd1);
    close(fd2);
    exit(1);
  }
  if (sendfile(1, 0, NULL, 10) == -1) {
    perror("sendfile" LOC);
    close(fd1);
    close(fd2);
    exit(1);
  }

  if (sendfile(1, 0, &offset, 10) == -1) {
    perror("sendfile" LOC);
    close(fd1);
    close(fd2);
    exit(1);
  }

#endif

  close(fd1);
  close(fd2);

  return 0;
}
