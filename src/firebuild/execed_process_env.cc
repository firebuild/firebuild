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

#include <memory>

#include "firebuild/execed_process_env.h"
#include "firebuild/debug.h"

namespace firebuild {

ExecedProcessEnv::ExecedProcessEnv(std::vector<std::shared_ptr<FileFD>>* fds,
                                   LaunchType launch_type)
    : argv_(), launch_type_(launch_type), type_flags_(), fds_(fds) { }

void ExecedProcessEnv::set_sh_c_command(const std::string &cmd) {
  argv_.push_back("sh");
  argv_.push_back("-c");
  argv_.push_back("--");
  argv_.push_back(cmd);
}

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const ExecedProcessEnv& env, const int level) {
  (void)level;  /* unused */
  return d(env.argv());
}
std::string d(const ExecedProcessEnv *env, const int level) {
  if (env) {
    return d(*env, level);
  } else {
    return "{ExecedProcessEnv NULL}";
  }
}

}  /* namespace firebuild */
