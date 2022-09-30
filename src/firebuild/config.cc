/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

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
#include <map>
#include <stdexcept>
#include <vector>

#include "common/firebuild_common.h"
#include "firebuild/debug.h"
#include "firebuild/exe_matcher.h"
#include "firebuild/file_name.h"

#define GLOBAL_CONFIG "/etc/firebuild.conf"
#define USER_CONFIG ".firebuild.conf"

namespace firebuild {

cstring_view_array ignore_locations {nullptr, 0, 0};
cstring_view_array system_locations {nullptr, 0, 0};

ExeMatcher* dont_shortcut_matcher = nullptr;
ExeMatcher* dont_intercept_matcher = nullptr;
ExeMatcher* skip_cache_matcher = nullptr;
/** Store results of processes consuming more CPU time (system + user) in microseconds than this. */
int64_t min_cpu_time_u = 0;
int shortcut_tries = 0;
int quirks = 0;

/** Parse configuration file
 *
 *  If custom_cfg_file is non-NULL, use that.
 *  Otherwise try ~/.firebuild.conf, or if that one does not exist then /etc/firebuild.conf.
 * */
static void parse_cfg_file(libconfig::Config *cfg, const char *custom_cfg_file) {
  /* we fall back to global configuration file */
  std::string cfg_file(GLOBAL_CONFIG);
  if (custom_cfg_file != NULL) {
    cfg_file = std::string(custom_cfg_file);
  } else {
    char *homedir = getenv("HOME");
    if (homedir != NULL) {
      std::string user_cfg_file = homedir + std::string("/" USER_CONFIG);
      int cfg_fd = open(user_cfg_file.c_str(), O_RDONLY);
      if (cfg_fd != -1) {
        /* fall back to private config file */
        cfg_file = user_cfg_file;
        close(cfg_fd);
      }
    }
  }
  try {
    cfg->readFile(cfg_file.c_str());
  }
  catch(const libconfig::FileIOException &fioex) {
    std::cerr << "Could not read configuration file " << cfg_file << std::endl;
    exit(EXIT_FAILURE);
  }
  catch(const libconfig::ParseException &pex) {
    std::cerr << "Parse error at " << pex.getFile() << ":" << pex.getLine()
              << " - " << pex.getError() << std::endl;
    exit(EXIT_FAILURE);
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
      default:
        std::cerr << "This type is not supported" << std::endl;
        exit(EXIT_FAILURE);
    }
  }

  delete mini_config;
}

static void init_locations(cstring_view_array* locations, const libconfig::Setting& items) {
  cstring_view_array_init(locations);
  for (int i = 0; i < items.getLength(); i++) {
    cstring_view_array_append(locations, strdup(items[i].c_str()));
  }
  cstring_view_array_sort(locations);
}

static void init_matcher(ExeMatcher **matcher, const libconfig::Setting& items) {
  assert(!*matcher);
  *matcher = new ExeMatcher();
  for (int i = 0; i < items.getLength(); i++) {
    (*matcher)->add(items[i].c_str());
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

  assert(FileName::isDbEmpty());
  init_locations(&ignore_locations, cfg->getRoot()["ignore_locations"]);
  init_locations(&system_locations, cfg->getRoot()["system_locations"]);

  init_matcher(&dont_shortcut_matcher, cfg->getRoot()["processes"]["dont_shortcut"]);
  init_matcher(&dont_intercept_matcher, cfg->getRoot()["processes"]["dont_intercept"]);
  init_matcher(&skip_cache_matcher, cfg->getRoot()["processes"]["skip_cache"]);

  if (cfg->exists("quirks")) {
    const libconfig::Setting& items = cfg->getRoot()["quirks"];
    for (int i = 0; i < items.getLength(); i++) {
      std::string quirk(items[i]);
      if (quirk == "ignore-tmp-listing") {
        quirks |= FB_QUIRK_IGNORE_TMP_LISTING;
      } else if (quirk == "lto-wrapper") {
        quirks |= FB_QUIRK_LTO_WRAPPER;
      } else {
        if (FB_DEBUGGING(FB_DEBUG_CONFIG)) {
          std::cerr <<"Ignoring unknown quirk: " + quirk << std::endl;
        }
      }
    }
  }
}

static void export_sorted_locations(libconfig::Config *cfg, const char* configuration_name,
                                    const std::string env_var_name,
                                    std::map<std::string, std::string>* env) {
  const libconfig::Setting& root = cfg->getRoot();
  const libconfig::Setting& locations_setting = root[configuration_name];
  std::vector<std::string> locations;
  for (int i = 0; i < locations_setting.getLength(); i++) {
    locations.emplace_back(locations_setting[i].c_str());
  }
  if (locations.size() > 0) {
    std::sort(locations.begin(), locations.end());
    std::string locations_appended;
    for (auto loc : locations) {
      if (locations_appended.length() == 0) {
        locations_appended.append(loc);
      } else {
        locations_appended.append(":" + loc);
      }
    }
    (*env)[env_var_name] = std::string(locations_appended);
    FB_DEBUG(FB_DEBUG_PROC, " " + env_var_name + "=" + (*env)[env_var_name]);
  }
}

char** get_sanitized_env(libconfig::Config *cfg, const char *fb_conn_string,
                         bool insert_trace_markers) {
  const libconfig::Setting& root = cfg->getRoot();

  FB_DEBUG(FB_DEBUG_PROC, "Passing through environment variables:");

  const libconfig::Setting& pass_through = root["env_vars"]["pass_through"];
  std::map<std::string, std::string> env;
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

  FB_DEBUG(FB_DEBUG_PROC, "Setting preset environment variables:");
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
      FB_DEBUG(FB_DEBUG_PROC, " " + var_name + "=" + env[var_name]);
    }
  }

  export_sorted_locations(cfg, "system_locations", "FB_SYSTEM_LOCATIONS", &env);
  export_sorted_locations(cfg, "ignore_locations", "FB_IGNORE_LOCATIONS", &env);

  const char *ld_preload_value = getenv("LD_PRELOAD");
  if (ld_preload_value) {
    env["LD_PRELOAD"] = LIBFIREBUILD_SO ":" + std::string(ld_preload_value);
  } else {
    env["LD_PRELOAD"] = LIBFIREBUILD_SO;
  }
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
