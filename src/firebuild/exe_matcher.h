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

#ifndef FIREBUILD_EXE_MATCHER_H_
#define FIREBUILD_EXE_MATCHER_H_

#include <tsl/hopscotch_set.h>

#include <string>

#include "firebuild/file_name.h"

namespace firebuild {

class ExecedProcess;

/**
 * Class for checking if either exe or arg0 matches any of the base names of full names (paths).
 */
class ExeMatcher {
 public:
  ExeMatcher() : base_names_(), full_names_() {}
  bool match(const ExecedProcess* const proc) const;
  bool match(const FileName* exe_file, const FileName* executed_file,
             const std::string& arg0) const;
  bool empty() const {return base_names_.empty() && full_names_.empty();}
  void add(const std::string name) {
    if (name.find('/') == std::string::npos) {
      base_names_.insert(name);
    } else {
      full_names_.insert(name);
    }
  }

 private:
  bool match(const std::string& exe) const;
  tsl::hopscotch_set<std::string> base_names_;
  tsl::hopscotch_set<std::string> full_names_;
};

}  /* namespace firebuild */
#endif  // FIREBUILD_EXE_MATCHER_H_
