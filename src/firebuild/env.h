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


#ifndef FIREBUILD_ENV_H_
#define FIREBUILD_ENV_H_

#include <string>
#include <vector>

namespace firebuild {

class Env {
 public:
  /** Returns the value of the given environment variable, or nullptr if not found.
   *  Usage: Env::get_var(env, "PATH")
   */
  template <size_t N>
  static const char* get_var(const std::vector<std::string>& env, const char (&var)[N],
                             size_t* out_len = nullptr) {
    for (const auto& e : env) {
      if (e.size() >= (N) && e.compare(0, N - 1, var) == 0 && e[N - 1] == '=') {
        if (out_len) {
          *out_len = e.size() - N;
        }
        return e.c_str() + N;
      }
    }
    return nullptr;
  }
};

}  // namespace firebuild

#endif  // FIREBUILD_ENV_H_
