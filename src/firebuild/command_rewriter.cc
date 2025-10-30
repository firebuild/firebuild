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


#include "firebuild/command_rewriter.h"

#include <string>
#include <vector>

#include "firebuild/config.h"
#include "firebuild/file_name.h"
#include "firebuild/hash_cache.h"
#include "firebuild/process.h"

namespace firebuild {

void CommandRewriter::maybe_rewrite(
    const FileName** executable,
    std::vector<std::string>* args,
    bool* rewritten_executable,
    bool* rewritten_args) {
  if (args->size() > 0
     && (args->at(0) == "sphinx-build" || args->at(0).ends_with("/sphinx-build"))
     && !dont_shortcut_matcher->match(args->at(0))) {
    /* None of the args is "-E" */
    bool has_E = false;
    for (const std::string& arg : *args) {
      if (arg == "-E") {
        has_E = true;
        break;
      }
    }
    if (!has_E) {
      args->insert(args->begin() + 1, "-E");
      *rewritten_args = true;
      return;
    }
  }
#ifndef __APPLE__
  bool is_static = false;
  if (qemu_user && hash_cache && hash_cache->get_is_static(*executable, &is_static) && is_static) {
    *executable = qemu_user;
    *rewritten_executable = true;
    args->insert(args->begin(), {(*executable)->to_string(), QEMU_LIBC_SYSCALLS_OPTION});
    *rewritten_args = true;
  }
#else
  (void)executable;
  (void)rewritten_executable;
#endif
}

}  // namespace firebuild
