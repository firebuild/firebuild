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

/* This binary is meant to be statically linked. */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TOSTR(x) TOSTR2(x)
#define TOSTR2(x) #x
#define LOC "[" __FILE__ ":" TOSTR(__LINE__) "]"

int main(int argc, char *argv[]) {
  puts("I am statically linked.");
  fflush(stdout);

  /* Test a vDSO call - that typically doesn't require kernel context switch */
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    perror("clock_gettime" LOC);
    exit(1);
  }

  if (argc > 1) {
    int recurse_level = 0;
    sscanf(argv[1], "%d", &recurse_level);
    while (recurse_level-- > 0) {
      pid_t child_pid = fork();
      if (child_pid > 0) {
        wait(NULL);
        return 0;
      }
    }
    return system("echo end");
  }
  return 0;
}
