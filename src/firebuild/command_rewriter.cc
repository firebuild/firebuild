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
#include "firebuild/utils.h"

namespace firebuild {

/* Return whether the command was modified. */
static bool add_argument(const std::string& executable_name_pattern,
                             const std::string& arg,
                             const std::string& executable_name,
                             std::vector<std::string>* args,
                             bool* rewritten_args) {
  if (executable_name == executable_name_pattern
     && !dont_shortcut_matcher->match(executable_name)) {
    for (const std::string& existing_arg : *args) {
      if (existing_arg == arg) {
        return false;
      }
    }
    args->insert(args->begin() + 1, arg);
    *rewritten_args = true;
    return true;
  }
  return false;
}

// TODO(rbalint): make executable_name_pattern a regex
static bool replace_argument(const std::string& executable_name_pattern,
                             const std::string& old_arg,
                             const std::vector<std::string>& new_args,
                             const std::string& executable_name,
                             std::vector<std::string>* args,
                             bool* rewritten_args) {
  if (executable_name == executable_name_pattern
     && !dont_shortcut_matcher->match(executable_name)) {
    for (size_t i = 0; i < args->size(); i++) {
      if (args->at(i) == old_arg) {
        args->erase(args->begin() + i);
        args->insert(args->begin() + i, new_args.begin(), new_args.end());
        *rewritten_args = true;
        i += new_args.size() - 1;
      }
    }
  }
  return *rewritten_args;
}

static bool has_argument(const std::string& arg,
                         const std::vector<std::string>& args) {
  for (const std::string& existing_arg : args) {
    if (existing_arg == arg) {
      return true;
    }
  }
  return false;
}

void CommandRewriter::maybe_rewrite(
    const FileName** executable,
    std::vector<std::string>* args,
    bool* rewritten_executable,
    bool* rewritten_args) {
  const std::vector<std::string>& emit_pch_new_args = {
    "-emit-pch",
    "-fno-pch-timestamp"
  };
  const std::vector<std::string>& emit_pch_new_xclang_args = {
    "-emit-pch",
    "-Xclang",
    "-fno-pch-timestamp"
  };
  if (args->size() > 0) {
    const std::string executable_name = base_name(args->at(0).c_str());
    if (add_argument("sphinx-build", "-E", executable_name, args, rewritten_args)) {
      /* nothing more to do, continue to static check */
    } else if ((executable_name.starts_with("clang"))
               && !has_argument("-fno-pch-timestamp", *args)) {
      replace_argument(executable_name, "-emit-pch",
                       has_argument("-cc1", *args) ?  emit_pch_new_args : emit_pch_new_xclang_args,
                       executable_name, args, rewritten_args);
      /* nothing more to do, continue to static check */
    }
  }
  /* Static executable check. */
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
