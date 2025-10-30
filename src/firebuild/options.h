/*
 * Copyright (c) 2024 Firebuild Inc.
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

#ifndef FIREBUILD_OPTIONS_H_
#define FIREBUILD_OPTIONS_H_

#include <list>
#include <string>

#include "firebuild/cxx_lang_utils.h"

namespace firebuild {

/**
 * Options handling
 */
class Options {
 public:
  static void parse(const int argc, char *argv[]);
  static void usage();
  static void free();
  static const char* config_file() {
    return config_file_;
  }
  static const char* directory() {
    return directory_;
  }
  static const char* report_file() {
    return report_file_;
  }
  static const char* const * build_cmd() {
    return build_cmd_;
  }
  static void prepend_to_build_cmd(const char* cmd);

  static const std::list<std::string>& config_strings() {
    return *config_strings_;
  }
  static bool quiet() {
    return quiet_;
  }
  static bool generate_report() {
    return generate_report_;
  }
  static bool insert_trace_markers() {
    return insert_trace_markers_;
  }
  static bool do_gc() {
    return do_gc_;
  }
  static bool print_stats() {
    return print_stats_;
  }
  static bool reset_stats() {
    return reset_stats_;
  }

 private:
  static char* config_file_;
  static char* directory_;
  static const char* report_file_;
  static const char** build_cmd_;
  static bool build_cmd_owned_;
  static std::list<std::string>* config_strings_;
  static bool quiet_;
  static bool generate_report_;
  static bool insert_trace_markers_;
  static bool do_gc_;
  static bool print_stats_;
  static bool reset_stats_;
};

}  /* namespace firebuild */

#endif  // FIREBUILD_OPTIONS_H_
