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

#ifndef FIREBUILD_CONFIG_H_
#define FIREBUILD_CONFIG_H_

#include <list>
#include <string>
#include <vector>

#include <libconfig.h++>

#include "common/firebuild_common.h"
#include "firebuild/exe_matcher.h"
#include "firebuild/file_name.h"

namespace firebuild {

/** global configuration */
extern libconfig::Config * cfg;

extern cstring_view_array ignore_locations;
extern cstring_view_array read_only_locations;
extern ExeMatcher* shortcut_allow_list_matcher;
extern ExeMatcher* dont_shortcut_matcher;
extern ExeMatcher* dont_intercept_matcher;
extern ExeMatcher* skip_cache_matcher;
extern tsl::hopscotch_set<std::string>* shells;
extern bool ccache_disabled;

/** Store results of processes consuming more CPU time (system + user) in microseconds than this. */
extern int64_t min_cpu_time_u;

/**
 * Give up after shortcut_tries and run the process without shortcutting it.
 * Value of 0 means trying all candidates.
 */
extern int shortcut_tries;
/**
 * Maximum size of the files stored in the cache, in bytes.
 */
extern int64_t max_cache_size;

/**
 * Maximum size of a single cache entry including the referenced objs.
 */
extern uint64_t max_entry_size;

/** Enabled quirks represented as flags. See "quirks" in etc/firebuild.conf. */
extern int quirks;
#define FB_QUIRK_IGNORE_TMP_LISTING  0x01
#define FB_QUIRK_LTO_WRAPPER         0x02
#define FB_QUIRK_GUESS_FILE_PARAMS   0x04
#define FB_QUIRK_IGNORE_TIME_QUERIES 0x08
#define FB_QUIRK_IGNORE_STATFS       0x10

void read_config(libconfig::Config *cfg, const char *custom_cfg_file,
                 const std::list<std::string>& config_strings);

/**
 * Construct a NULL-terminated array of "NAME=VALUE" environment variables
 * for the build command. The returned stings and array must be free()-d.
 *
 * TODO: detect duplicates
 */
char** get_sanitized_env(libconfig::Config *cfg, const char* fb_conn_string,
                         bool insert_trace_markers);

}  /* namespace firebuild */
#endif  // FIREBUILD_CONFIG_H_
