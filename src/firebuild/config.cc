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
#include <unistd.h>

#include <algorithm>
#include <string>
#include <iostream>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

#include "common/firebuild_common.h"
#include "firebuild/debug.h"
#include "firebuild/exe_matcher.h"
#include "firebuild/file_name.h"

#define GLOBAL_CONFIG "/etc/firebuild.conf"
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
uint64_t max_entry_size = 0;
int quirks = 0;

/**
 * Parse configuration file
 *
 * If custom_cfg_file is non-NULL, use that.
 * Otherwise try ./firebuild.conf, ~/.firebuild.conf, $XDG_CONFIG_HOME/firebuild/firebuild.conf,
 * /etc/firebuild.conf in that order.
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

  assert(FileName::isDbEmpty());
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

char** get_sanitized_env(libconfig::Config *cfg, const char *fb_conn_string,
                         bool insert_trace_markers) {
  const libconfig::Setting& root = cfg->getRoot();

  std::map<std::string, std::string> env;
  FB_DEBUG(FB_DEBUG_PROC, "Passing through environment variables:");
  try {
    const libconfig::Setting& pass_through = root["env_vars"]["pass_through"];
    for (int i = 0; i < pass_through.getLength(); i++) {
      std::string pass_through_env(pass_through[i].c_str());
      char * got_env = getenv(pass_through_env.c_str());
      if (got_env != NULL) {
        env[pass_through_env] = std::string(got_env);
        FB_DEBUG(FB_DEBUG_PROC, " " + std::string(pass_through_env) + "="
                 + env[pass_through_env]);
      }
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

  try {
    const libconfig::Setting& locations_setting = root["processes"]["jobserver_users"];
    export_sorted(locations_setting, "FB_JOBSERVER_USERS", &env);
  } catch(libconfig::SettingNotFoundException&) {
    /* Configuration setting may be missing. This is OK. */
  }

  const char *ld_preload_value = getenv(LD_PRELOAD);
  if (ld_preload_value) {
    env[LD_PRELOAD] = LIBFIREBUILD_SO ":" + std::string(ld_preload_value);
  } else {
    env[LD_PRELOAD] = LIBFIREBUILD_SO;
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

}  /* namespace firebuild */
