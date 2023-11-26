/*
 * Copyright (c) 2023 Firebuild Inc.
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

/* Test pthreads interception. */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define TOSTR(x) TOSTR2(x)
#define TOSTR2(x) #x
#define LOC "[" __FILE__ ":" TOSTR(__LINE__) "]"

/* Try to open a file passed as the argument that not expected to exist. */
static void* pthread_open_notexists(void *arg) {
  int fd = open((const char*)arg, O_RDONLY);
  if (fd != -1) {
    fprintf(stderr, "open" LOC " should have failed\n");
    exit(1);
  }
  return NULL;
}

int main() {
  pthread_t thread;

  int ret = pthread_create(&thread, NULL, pthread_open_notexists, "test_pthread_notexists");
  if (ret == -1) {
    errno = ret;
    perror("pthread_create");
    return 1;
  }

  if (pthread_join(thread, NULL) < 0) {
    perror("pthread_join");
    return 1;
  }

  return 0;
}
