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

extern char **environ;

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
  /* First argument is libfirebuild to add to LD_PRELOAD. */
  assert(argc == 2);
  (void)argc;
#ifdef __APPLE__
  (void)argv;
  unsetenv("DYLD_INSERT_LIBRARIES");
#else
  char modified_ld_preload[4096];
  snprintf(modified_ld_preload, sizeof(modified_ld_preload),
           "LD_PRELOAD=  LIBXXX.SO  %s  LIBYYY.SO", argv[1]);
  putenv(modified_ld_preload);
#endif
  setenv("BBB", "bbb", 0);

  return system("printenv");
}
