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

#include <getopt.h>

#include "firebuild/options.h"

#include "common/config.h"
#include "firebuild/debug.h"

namespace firebuild {

char* Options::config_file_ = nullptr;
char* Options::directory_ = nullptr;
const char* Options::report_file_ = "firebuild-build-report.html";
const char** Options::build_cmd_ = nullptr;
bool Options::build_cmd_owned_ = false;
std::list<std::string>* Options::config_strings_ = nullptr;
bool Options::quiet_ = false;
bool Options::generate_report_ = false;
bool Options::insert_trace_markers_ = false;
bool Options::do_gc_ = false;
bool Options::print_stats_ = false;
bool Options::reset_stats_ = false;

void Options::usage() {
  printf(
      "Usage: firebuild [OPTIONS] <BUILD COMMAND>\n"
      "Execute BUILD COMMAND with Firebuild instrumentation\n"
      "\n"
      "Mandatory arguments to long options are mandatory for short options too.\n"
      "  -c, --config-file=FILE       Use FILE as configuration file.\n"
      "                               If not specified, load .firebuild.conf, ~/.firebuild.conf,\n"
      "                               $XDG_CONFIG_HOME/firebuild/firebuild.conf or\n"
      "                               " SYSCONFDIR "/firebuild.conf in that order.\n"
      "  -C, --directory=DIR          change directory before running the command\n"
      "  -d, --debug-flags=list       comma separated list of debug flags,\n"
      "                               \"-d help\" to get a list.\n"
      "  -D, --debug-filter=list      comma separated list of commands to debug.\n"
      "                               Debug messages related to processes which are not listed\n"
      "                               are suppressed.\n"
      "  -g, --gc                     Garbage collect the cache.\n"
      "                               Keeps debugging entries related to kept files when used\n"
      "                               together with \"--debug cache\".\n"
      "  -r, --generate-report[=HTML] generate a report on the build command execution.\n"
      "                               the report's filename can be specified \n"
      "                               (firebuild-build-report.html by default). \n"
      "  -h, --help                   show this help\n"
      "  -o, --option=key=val         Add or replace a scalar in the config\n"
      "  -o, --option=key=[]          Clear an array in the config\n"
      "  -o, --option=key+=val        Append to an array of scalars in the config\n"
      "  -o, --option=key-=val        Remove from an array of scalars in the config\n"
      "  -q, --quiet                  Quiet; print error messages only from firebuild.\n"
      "                               The BUILD COMMAND's messages are not affected.\n"
      "  -s, --show-stats             Show cache hit statistics.\n"
      "  -z, --zero-stats             Zero cache hit statistics.\n"
      "  -i, --insert-trace-markers   perform open(\"/FIREBUILD <debug_msg>\", 0) calls\n"
      "                               to let users find unintercepted calls using\n"
      "                               strace or ltrace. This works in debug builds only.\n"
      "      --version                output version information and exit\n"
      "Exit status:\n"
      " exit status of the BUILD COMMAND\n"
      " 1  in case of failure\n");
}

void Options::parse(const int argc, char *argv[]) {
  config_strings_ = new std::list<std::string>();
  while (1) {
    int option_index = 0;
    static struct option long_options[] = {
      {"config-file",          required_argument, 0, 'c' },
      {"gc",                   no_argument,       0, 'g' },
      {"directory",            required_argument, 0, 'C' },
      {"debug-flags",          required_argument, 0, 'd' },
      {"debug-filter",         required_argument, 0, 'D' },
      {"generate-report",      optional_argument, 0, 'r' },
      {"help",                 no_argument,       0, 'h' },
      {"option",               required_argument, 0, 'o' },
      {"quiet",                no_argument,       0, 'q' },
      {"show-stats",           no_argument,       0, 's' },
      {"zero-stats",           no_argument,       0, 'z' },
      {"insert-trace-markers", no_argument,       0, 'i' },
      {"version",              no_argument,       0, 'v' },
      {0,                                0,       0,  0  }
    };

    int c = getopt_long(argc, argv, "c:C:d:D:r::o:qghisz",
                        long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
      case 'c':
        config_file_ = optarg;
        break;

      case 'C':
        directory_ = optarg;
        break;

      case 'd':
        /* Merge the values, so that multiple '-d' options are also allowed. */
        firebuild::debug_flags |= firebuild::parse_debug_flags(optarg);
        break;

      case 'g':
        do_gc_ = true;
        break;

      case 'D':
        firebuild::init_debug_filter(optarg);
        break;

      case 'h':
        usage();
        exit(EXIT_SUCCESS);
        /* break; */

      case 'o':
        if (optarg != NULL) {
          config_strings_->push_back(std::string(optarg));
        } else {
          usage();
          exit(EXIT_FAILURE);
        }
        break;

      case 'i':
#ifdef FB_EXTRA_DEBUG
        insert_trace_markers_ = true;
#endif
        break;

      case 'q':
        quiet_ = true;
        break;

      case 'r':
        generate_report_ = true;
        if (optarg != NULL) {
          report_file_ = optarg;
        }
        break;

      case 's':
        print_stats_ = true;
        break;

      case 'v':
        printf("Firebuild " FIREBUILD_VERSION "\n\n"
               "Copyright (c) 2022 Firebuild Inc.\n"
               "All rights reserved.\n"
               "Free for personal use and commercial trial.\n"
               "Non-trial commercial use requires licenses available from https://firebuild.com.\n"
               "\n"
               "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
               "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
               "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n"
               "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
               "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n"
               "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n"
               "SOFTWARE.\n");
        exit(EXIT_SUCCESS);
        break;

      case 'z':
        reset_stats_ = true;
        break;

      default:
        usage();
        exit(EXIT_FAILURE);
    }
  }

  if (optind >= argc) {
    if (!do_gc_ && !print_stats_ && !reset_stats_) {
      usage();
      exit(EXIT_FAILURE);
    }
  } else {
    if (do_gc_) {
      printf("The --gc (or -g) option can be used only without a BUILD COMMAND.");
      exit(EXIT_FAILURE);
    }
  }

  if (argc > optind) {
    build_cmd_ = const_cast<const char**>(argv + optind);
  }
}

void Options::prepend_to_build_cmd(const char* cmd) {
  size_t build_cmd_len = 0;
  while (build_cmd_ && build_cmd_[build_cmd_len]) {
    build_cmd_len++;
  }
  const char** new_build_cmd =
      static_cast<const char**>(malloc(sizeof(char*) * (build_cmd_len + 2)));
  new_build_cmd[0] = cmd;
  for (size_t i = 0; i <= build_cmd_len; i++) {
    new_build_cmd[i + 1] = build_cmd_[i];
  }
  if (build_cmd_owned_) {
    ::free(build_cmd_);
  }
  build_cmd_ = new_build_cmd;
  build_cmd_owned_ = true;
}

void Options::free() {
  delete(config_strings_);
  if (build_cmd_owned_) {
    ::free(build_cmd_);
  }
}

}  /* namespace firebuild */
