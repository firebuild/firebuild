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

#include "firebuild/config.h"

#include <string.h>
#include <fcntl.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <unistd.h>

#include <algorithm>
#include <string>
#include <iostream>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <limits>
#include <map>
#include <regex>
#include <stdexcept>
#include <vector>

#include "common/config.h"
#include "common/firebuild_common.h"
#include "common/platform.h"
#include "firebuild/debug.h"
#include "firebuild/exe_matcher.h"
#include "firebuild/file_name.h"
#include "firebuild/hash_cache.h"

#define GLOBAL_CONFIG SYSCONFDIR"/firebuild.conf"
#define USER_CONFIG ".firebuild.conf"
#define XDG_CONFIG "firebuild/firebuild.conf"

namespace firebuild {

libconfig::Config * cfg = nullptr;

cstring_view_array ignore_locations {nullptr, 0, 0};
cstring_view_array read_only_locations {nullptr, 0, 0};

ExeMatcher* shortcut_allow_list_matcher = nullptr;
ExeMatcher* dont_shortcut_matcher = nullptr;
ExeMatcher* dont_intercept_matcher = nullptr;
ExeMatcher* skip_cache_matcher = nullptr;
tsl::hopscotch_set<std::string>* shells = nullptr;
bool ccache_disabled = false;
/** Store results of processes consuming more CPU time (system + user) in microseconds than this. */
int64_t min_cpu_time_u = 0;
int shortcut_tries = 0;
int64_t max_cache_size = 0;
off_t max_entry_size = 0;
off_t max_inline_blob_size = 4096;  /* Default 4KB */
int quirks = 0;

#ifndef __APPLE__
const FileName* qemu_user = nullptr;
#endif

/**
 * Parse configuration file
 *
 * If custom_cfg_file is non-NULL, use that.
 * Otherwise try ./firebuild.conf, ~/.firebuild.conf, $XDG_CONFIG_HOME/firebuild/firebuild.conf,
 * SYSCONFDIR/firebuild.conf in that order.
 */
static void parse_cfg_file(libconfig::Config *cfg, const char *custom_cfg_file) {
  std::vector<std::string> cfg_files;
  if (custom_cfg_file != NULL) {
    cfg_files = {custom_cfg_file};
  } else {
    cfg_files = {".firebuild.conf"};
    char *homedir = getenv("HOME");
    if (homedir != NULL) {
      std::string user_cfg_file = homedir + std::string("/" USER_CONFIG);
      cfg_files.push_back(user_cfg_file);
    }
    char *xdg_config_home = getenv("XDG_CONFIG_HOME");
    if (xdg_config_home != NULL) {
      std::string user_cfg_file = xdg_config_home + std::string("/" XDG_CONFIG);
      cfg_files.push_back(user_cfg_file);
    }
    cfg_files.push_back(GLOBAL_CONFIG);
  }
  for (size_t i = 0; i < cfg_files.size(); i++) {
    try {
      cfg->readFile(cfg_files[i].c_str());
      break;
    }
    catch (const libconfig::FileIOException &fioex) {
      if (i == cfg_files.size() - 1) {
        std::cerr << "Could not read configuration file " << cfg_files[i] << std::endl;
        exit(EXIT_FAILURE);
      } else {
        continue;
      }
    }
    catch (const libconfig::ParseException &pex) {
      std::cerr << "Parse error at " << pex.getFile() << ":" << pex.getLine()
                << " - " << pex.getError() << std::endl;
      exit(EXIT_FAILURE);
    }
  }
}

/** Modify configuration
 *
 *  str is one of:
 *    key = value:
 *      Create or replace the existing `key` to contain the scalar `value`
 *    key += value
 *      Append the scalar `value` to the existing array `key`
 *    key -= value
 *      Remove the scalar `value` from the existing array `key`, if found
 *    key = "[]":
 *      Clear an array
 *
 *  E.g. str = "processes.dont_shortcut += \"myapp\""
 *
 *  Currently only strings are supported, but it's easy to add TypeBoolean,
 *  TypeInt, TypeInt64 and TypeFloat too, if required.
 */
static void modify_config(libconfig::Config *cfg, const std::string& str) {
  bool append = false;
  bool remove = false;

  size_t eq_pos = str.find('=');
  if (eq_pos == std::string::npos) {
    std::cerr << "-o requires an equal sign" << std::endl;
    exit(EXIT_FAILURE);
  }

  size_t name_end_pos = eq_pos;
  if (name_end_pos > 0) {
    if (str[name_end_pos - 1] == '+') {
      name_end_pos--;
      append = true;
    } else if (str[name_end_pos - 1 ] == '-') {
      name_end_pos--;
      remove = true;
    }
  }
  while (name_end_pos > 0 && str[name_end_pos - 1] == ' ') {
    name_end_pos--;
  }
  std::string name = str.substr(0, name_end_pos);

  /* We support operations with scalars (string, int, bool...).
   * Libconfig doesn't provide a direct method to parse or even
   * get the type of the argument. So create and parse a mini config. */
  std::string mini_config_str = "x = " + str.substr(eq_pos + 1);
  libconfig::Config *mini_config = new libconfig::Config();
  mini_config->readString(mini_config_str);
  libconfig::Setting& x = mini_config->getRoot()["x"];
  libconfig::Setting::Type type = x.getType();

  if (append) {
    /* Append scalar value to an existing array. */
    try {
      libconfig::Setting& array = cfg->lookup(name);
      libconfig::Setting& adding = array.add(type);
      /* Unfortunately there's no operator= to assign from another Setting. */
      switch (type) {
        case libconfig::Setting::TypeString: {
          std::string x_str(x.c_str());
          adding = x_str.c_str();
          break;
        }
        default:
          std::cerr << "This type is not supported" << std::endl;
          exit(EXIT_FAILURE);
      }
    } catch(libconfig::SettingNotFoundException&) {
      std::cerr << "Setting not found: " << name << std::endl;
      exit(EXIT_FAILURE);
    }
  } else if (remove) {
    /* Remove all occurrences of a scalar value from an existing array. */
    libconfig::Setting& array = cfg->lookup(name);
    for (int i = 0; i < array.getLength(); i++) {
      libconfig::Setting& item = array[i];
      /* Unfortunately there's no operator== to compare with another Setting. */
      switch (type) {
        case libconfig::Setting::TypeString: {
          std::string item_str(item.c_str());
          std::string x_str(x.c_str());
          if (item_str == x_str) {
            array.remove(i);
            i--;
          }
          break;
        }
        default:
          std::cerr << "This type is not supported" << std::endl;
          exit(EXIT_FAILURE);
      }
    }
  } else {
    if (type == libconfig::Setting::TypeArray) {
      if (x.getLength() > 0) {
        std::cerr << "Arrays can only be reset" << std::endl;
        exit(EXIT_FAILURE);
      }
      try {
        libconfig::Setting& array = cfg->lookup(name);
        const std::string setting_name = array.getName();
        libconfig::Setting& parent = array.getParent();
        parent.remove(setting_name);
        parent.add(setting_name, type);
      } catch(libconfig::SettingNotFoundException&) {
        std::cerr << "Setting not found" << std::endl;
        exit(EXIT_FAILURE);
      }
      delete mini_config;
      return;
    }
    /* Set a given value, overwriting the previous value if necessary. */
    try {
      cfg->getRoot().remove(name);
    } catch(libconfig::SettingNotFoundException&) {}
    libconfig::Setting& adding = cfg->getRoot().add(name, type);
    /* Unfortunately there's no operator= to assign from another Setting. */
    switch (type) {
      case libconfig::Setting::TypeString: {
        std::string x_str(x.c_str());
        adding = x_str.c_str();
        break;
      }
      case libconfig::Setting::TypeFloat: {
        float x_float = x;
        adding = x_float;
        break;
      }
      case libconfig::Setting::TypeInt: {
        int x_int = x;
        adding = x_int;
        break;
      }
      default:
        std::cerr << "This type is not supported" << std::endl;
        exit(EXIT_FAILURE);
    }
  }

  delete mini_config;
}

static void init_locations(cstring_view_array* locations, const libconfig::Config *cfg,
                           const char* locations_setting) {
  cstring_view_array_init(locations);
  try {
    const libconfig::Setting& items = cfg->getRoot()[locations_setting];
    for (int i = 0; i < items.getLength(); i++) {
      cstring_view_array_append(locations, strdup(items[i].c_str()));
    }
  } catch(libconfig::SettingNotFoundException&) {
    /* Configuration setting may be missing. This is OK. */
  }
  cstring_view_array_sort(locations);
}

static void init_matcher(ExeMatcher **matcher, const libconfig::Config *cfg,
                         const char* matcher_setting) {
  assert(!*matcher);
  *matcher = new ExeMatcher();
  try {
    const libconfig::Setting& items = cfg->getRoot()["processes"][matcher_setting];
    for (int i = 0; i < items.getLength(); i++) {
      (*matcher)->add(items[i].c_str());
    }
  } catch(libconfig::SettingNotFoundException&) {
    /* Configuration setting may be missing. This is OK. */
  }
}

void read_config(libconfig::Config *cfg, const char *custom_cfg_file,
                 const std::list<std::string> &config_strings) {
  parse_cfg_file(cfg, custom_cfg_file);
  cfg->setAutoConvert(true);
  for (auto s : config_strings) {
    modify_config(cfg, s);
  }

  if (FB_DEBUGGING(FB_DEBUG_CONFIG)) {
    fprintf(stderr, "--- Config:\n");
    cfg->write(stderr);
    fprintf(stderr, "--- End of config.\n");
  }

  /* Save portions of the configuration to separate variables for faster access. */
  if (cfg->exists("min_cpu_time")) {
    libconfig::Setting& min_cpu_time_cfg = cfg->getRoot()["min_cpu_time"];
    if (min_cpu_time_cfg.isNumber()) {
      float min_cpu_time_s = min_cpu_time_cfg;
      min_cpu_time_u = 1000000.0 * min_cpu_time_s;
    }
  }

  if (cfg->exists("shortcut_tries")) {
    libconfig::Setting& shortcut_tries_cfg = cfg->getRoot()["shortcut_tries"];
    if (shortcut_tries_cfg.isNumber()) {
      shortcut_tries = shortcut_tries_cfg;
    }
  }

  if (cfg->exists("max_cache_size")) {
    libconfig::Setting& max_cache_size_cfg = cfg->getRoot()["max_cache_size"];
    if (max_cache_size_cfg.isNumber()) {
      double max_cache_size_gb = max_cache_size_cfg;
      max_cache_size = max_cache_size_gb * 1000000000;
      if (max_cache_size < 0) {
        /* Fix up negative numbers. */
        max_cache_size = 0;
      }
    }
  }

  if (cfg->exists("max_entry_size")) {
    libconfig::Setting& max_entry_size_cfg = cfg->getRoot()["max_entry_size"];
    if (max_entry_size_cfg.isNumber()) {
      double max_entry_size_mb = max_entry_size_cfg;
      if (max_entry_size_mb < 0) {
        /* Fix up negative numbers. */
        max_entry_size_mb = 0;
      }
      max_entry_size = max_entry_size_mb * 1000000;
    }
  }

  if (cfg->exists("max_inline_blob_size")) {
    libconfig::Setting& max_inline_blob_size_cfg = cfg->getRoot()["max_inline_blob_size"];
    if (max_inline_blob_size_cfg.isNumber()) {
      double max_inline_blob_size_kb = max_inline_blob_size_cfg;
      if (max_inline_blob_size_kb < 0) {
        /* Fix up negative numbers. */
        max_inline_blob_size_kb = 0;
      }
      max_inline_blob_size = max_inline_blob_size_kb * 1024;
    }
  }

  assert(FileName::isDbEmpty());

#ifndef __APPLE__
  if (cfg->exists("qemu_user")) {
    libconfig::Setting& qemu_user_cfg = cfg->getRoot()["qemu_user"];
    qemu_user = FileName::Get(qemu_user_cfg.c_str());
  }
#endif

  init_locations(&ignore_locations, cfg, "ignore_locations");
  init_locations(&read_only_locations, cfg, "read_only_locations");
  /* The read_only_locations setting used to be called system_locations. */
  try {
    const libconfig::Setting& items = cfg->getRoot()["system_locations."];
    for (int i = 0; i < items.getLength(); i++) {
      cstring_view_array_append(&read_only_locations, strdup(items[i].c_str()));
    }
    cstring_view_array_sort(&read_only_locations);
  } catch(libconfig::SettingNotFoundException&) {
    /* Configuration setting may be missing. This is OK. */
  }

  init_matcher(&shortcut_allow_list_matcher, cfg, "shortcut_allow_list");
  if (shortcut_allow_list_matcher->empty()) {
    delete(shortcut_allow_list_matcher);
    shortcut_allow_list_matcher = nullptr;
  }
  init_matcher(&dont_shortcut_matcher, cfg, "dont_shortcut");
  init_matcher(&dont_intercept_matcher, cfg, "dont_intercept");
  init_matcher(&skip_cache_matcher, cfg, "skip_cache");

  shells = new tsl::hopscotch_set<std::string>();
  try {
    libconfig::Setting& shells_cfg = cfg->getRoot()["processes"]["shells"];
    for (int i = 0; i < shells_cfg.getLength(); i++) {
      shells->emplace(shells_cfg[i]);
    }
  } catch(libconfig::SettingNotFoundException&) {
    /* Configuration setting may be missing. This is OK. */
  }

  if (cfg->exists("quirks")) {
    const libconfig::Setting& items = cfg->getRoot()["quirks"];
    for (int i = 0; i < items.getLength(); i++) {
      std::string quirk(items[i]);
      if (quirk == "ignore-tmp-listing") {
        quirks |= FB_QUIRK_IGNORE_TMP_LISTING;
      } else if (quirk == "lto-wrapper") {
        quirks |= FB_QUIRK_LTO_WRAPPER;
      } else if (quirk == "ignore-time-queries") {
        quirks |= FB_QUIRK_IGNORE_TIME_QUERIES;
      } else if (quirk == "ignore-statfs") {
        quirks |= FB_QUIRK_IGNORE_STATFS;
      } else if (quirk == "guess-file-params") {
        quirks |= FB_QUIRK_GUESS_FILE_PARAMS;
      } else {
        if (FB_DEBUGGING(FB_DEBUG_CONFIG)) {
          std::cerr <<"Ignoring unknown quirk: " + quirk << std::endl;
        }
      }
    }
  }
}

static void export_sorted(const libconfig::Setting& setting,
                          const std::string env_var_name,
                          std::map<std::string, std::string>* env) {
  std::vector<std::string> entries;
  for (int i = 0; i < setting.getLength(); i++) {
    entries.emplace_back(setting[i].c_str());
  }

  if (entries.size() > 0) {
    std::sort(entries.begin(), entries.end());
    std::string entries_appended;
    for (auto entry : entries) {
      if (entries_appended.length() == 0) {
        entries_appended.append(entry);
      } else {
        entries_appended.append(":" + entry);
      }
    }
    (*env)[env_var_name] = std::string(entries_appended);
    FB_DEBUG(FB_DEBUG_PROC, " " + env_var_name + "=" + (*env)[env_var_name]);
  }
}

static void export_sorted_locations(libconfig::Config *cfg, const char* configuration_name,
                                    const std::string env_var_name,
                                    std::map<std::string, std::string>* env) {
  const libconfig::Setting& root = cfg->getRoot();
  try {
    const libconfig::Setting& locations_setting = root[configuration_name];
    export_sorted(locations_setting, env_var_name, env);
  } catch(libconfig::SettingNotFoundException&) {
    /* Configuration setting may be missing. This is OK. */
  }
}

static void add_pass_through_regex_matched_env_vars(
    std::map<std::string, std::string>* env,
    const std::vector<std::string>& pass_through_env_regexps) {

  if (pass_through_env_regexps.size() < 1) {
    /* Not much to do. */
    return;
  }

  /* Combine all regular expressions to one. */
  std::string combined_regex_string("(^" + pass_through_env_regexps[0] + "$)");
  for (size_t i = 1; i < pass_through_env_regexps.size(); i++) {
    combined_regex_string += "|(^" + pass_through_env_regexps[i] + "$)";
  }
  std::regex combined_regex(combined_regex_string);

  /* Match each set environment variable against the combined regex. */
  char** env_var = environ;
  while (*env_var != nullptr) {
    const char* name_end_ptr = std::strchr(*env_var, '=');
    if (name_end_ptr) {
      std::string name(*env_var, name_end_ptr - *env_var);
      if (std::regex_match(name, combined_regex)) {
        /* Avoid adding an environment variable multiple times. */
        if (env->find(name) == env->end()) {
          (*env)[name] = std::string(name_end_ptr + 1);
          FB_DEBUG(FB_DEBUG_PROC, " " + std::string(name) + "=" + (*env)[name]);
        }
      }
    }
    env_var++;
  }
}

static std::string libfirebuild_so() {
  char self_path_buf[FB_PATH_BUFSIZE];
#ifdef __APPLE__
  uint32_t r = sizeof(self_path_buf);
  if (_NSGetExecutablePath(self_path_buf, &r) == 0) {
    r = strlen(self_path_buf);
  } else {
    return FB_INTERCEPTOR_FULL_LIBDIR "/" LIBFIREBUILD_SO;
  }
#else
  ssize_t r = readlink("/proc/self/exe", self_path_buf, FB_PATH_BUFSIZE - 1);
  if (r <= 0) {
    return LIBFIREBUILD_SO;
  }
#endif
  std::string self_path(self_path_buf, r);
  if (self_path.ends_with("src/firebuild/firebuild")) {
    self_path.resize(self_path.size() - strlen("firebuild/firebuild"));
    return self_path + "interceptor/" + LIBFIREBUILD_SO;
  } else {
#ifdef __APPLE__
    return FB_INTERCEPTOR_FULL_LIBDIR "/" LIBFIREBUILD_SO;
#else
    return LIBFIREBUILD_SO;
#endif
  }
}

char** get_sanitized_env(libconfig::Config *cfg, const char *fb_conn_string,
                         bool insert_trace_markers) {
  const libconfig::Setting& root = cfg->getRoot();

  std::map<std::string, std::string> env;
  FB_DEBUG(FB_DEBUG_PROC, "Passing through environment variables:");
  try {
    const libconfig::Setting& pass_through = root["env_vars"]["pass_through"];
    std::vector<std::string> pass_through_env_regexps;
    std::regex exact_env_var("^[a-zA-Z_0-9]+$");
    for (int i = 0; i < pass_through.getLength(); i++) {
      std::string pass_through_env(pass_through[i].c_str());
      if (std::regex_match(pass_through_env, exact_env_var)) {
        char * got_env = getenv(pass_through_env.c_str());
        if (got_env != NULL) {
          env[pass_through_env] = std::string(got_env);
          FB_DEBUG(FB_DEBUG_PROC, " " + std::string(pass_through_env) + "="
                   + env[pass_through_env]);
        }
      } else {
        pass_through_env_regexps.push_back(pass_through_env);
      }
    }
    if (pass_through_env_regexps.size() > 0) {
      add_pass_through_regex_matched_env_vars(&env, pass_through_env_regexps);
    }
    FB_DEBUG(FB_DEBUG_PROC, "");
  } catch(libconfig::SettingNotFoundException&) {
    /* Configuration setting may be missing. This is OK. */
  }

  FB_DEBUG(FB_DEBUG_PROC, "Setting preset environment variables:");
  try {
    const libconfig::Setting& preset = root["env_vars"]["preset"];
    for (int i = 0; i < preset.getLength(); i++) {
      std::string str(preset[i].c_str());
      size_t eq_pos = str.find('=');
      if (eq_pos == std::string::npos) {
        fb_error("Invalid present environment variable: " + str);
        abort();
      } else {
        const std::string var_name = str.substr(0, eq_pos);
        env[var_name] = str.substr(eq_pos + 1);
        if (str == "CCACHE_DISABLE=1") {
          ccache_disabled = true;
        }
        FB_DEBUG(FB_DEBUG_PROC, " " + var_name + "=" + env[var_name]);
      }
    }
  } catch(libconfig::SettingNotFoundException&) {
    /* Configuration setting may be missing. This is OK. */
  }


  export_sorted_locations(cfg, "read_only_locations", "FB_READ_ONLY_LOCATIONS", &env);
  export_sorted_locations(cfg, "ignore_locations", "FB_IGNORE_LOCATIONS", &env);

  const char *ld_preload_value = getenv(LD_PRELOAD);
  if (ld_preload_value) {
    env[LD_PRELOAD] = libfirebuild_so() + ":" + std::string(ld_preload_value);
  } else {
    env[LD_PRELOAD] = libfirebuild_so();
  }
  FB_DEBUG(firebuild::FB_DEBUG_PROC, " " LD_PRELOAD "=" + env[LD_PRELOAD]);

#ifdef __APPLE__
  env["DYLD_FORCE_FLAT_NAMESPACE"] = "0";
  FB_DEBUG(firebuild::FB_DEBUG_PROC, " DYLD_FORCE_FLAT_NAMESPACE=" +
           env["DYLD_FORCE_FLAT_NAMESPACE"]);

#endif
  env["FB_SOCKET"] = fb_conn_string;
  FB_DEBUG(FB_DEBUG_PROC, " FB_SOCKET=" + env["FB_SOCKET"]);

  FB_DEBUG(FB_DEBUG_PROC, "");

#ifdef FB_EXTRA_DEBUG
  if (insert_trace_markers) {
    env["FB_INSERT_TRACE_MARKERS"] = "1";
  }
#else
  (void)insert_trace_markers;
#endif

  char ** ret_env =
      static_cast<char**>(malloc(sizeof(char*) * (env.size() + 1)));

  auto it = env.begin();
  int i = 0;
  while (it != env.end()) {
    ret_env[i] = strdup(std::string(it->first + "=" + it->second).c_str());
    it++;
    i++;
  }
  ret_env[i] = NULL;

  return ret_env;
}

#ifndef __APPLE__
void detect_qemu_user(const char* path) {
  assert(hash_cache);

  for (const char* candidate : {"qemu-user-interposable", "qemu-" C_COMPILER_TARGET_ARCH}) {
    qemu_user = hash_cache->resolve_command(
        candidate, strlen(candidate), path, strlen(path), nullptr);
    /* Check if qemu-user binary is dynamically linked as required for intercepting. */
    if (qemu_user) {
      bool is_static = false;
      if (hash_cache->get_is_static(qemu_user, &is_static)) {
        if (is_static) {
          fb_error("The qemu-user binary (" + qemu_user->to_string() + ") is statically linked. "
                  "Firebuild requires a dynamically linked qemu-user binary for interception.");
          qemu_user = nullptr;
        }
      } else {
        fb_error("Could not stat the qemu-user binary (" + qemu_user->to_string() + ").");
        qemu_user = nullptr;
      }
      /* Check if it supports -libc-syscalls options */
      if (qemu_user) {
        std::string qemu_libc_syscalls_check_cmd =
            qemu_user->to_string() + " -libc-syscalls --version 2>&1";
        FILE* pipe = popen(qemu_libc_syscalls_check_cmd.c_str(), "r");
        if (!pipe) {
          fb_error("Could not run the qemu-user binary (" + qemu_user->to_string() + ").");
          qemu_user = nullptr;
        } else {
          char buffer[1024];
          std::string result = "";
          while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
            result += buffer;
          }
          int ret_code = pclose(pipe);
          if (ret_code != 0 || result.find("-libc-syscalls") != std::string::npos) {
            fb_error("The qemu-user binary (" + qemu_user->to_string()
                    + ") does not support the -libc-syscalls option required for interception.");
            fb_error("Exit code: " + d(ret_code) + ", output: " + result);
            qemu_user = nullptr;
          }
        }
      }
    }
    if (qemu_user) {
      break;
    }
  }

  if (FB_DEBUGGING(FB_DEBUG_CONFIG)) {
    std::cerr << "Using qemu-user binary: "
              << (qemu_user ? qemu_user->c_str() : "not found") << std::endl;
  }
}
#endif
}  /* namespace firebuild */
