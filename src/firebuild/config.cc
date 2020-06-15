/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include "firebuild/config.h"

#include <fcntl.h>
#include <unistd.h>

#include <string>
#include <iostream>
#include <cerrno>
#include <cstdio>
#include <stdexcept>

#include "firebuild/debug.h"

#define GLOBAL_CONFIG "/etc/firebuild.conf"
#define USER_CONFIG ".firebuild.conf"

namespace firebuild {

/** Parse configuration file
 *
 *  If custom_cfg_file is non-NULL, use that.
 *  Otherwise try ~/.firebuild.conf, or if that one does not exist then /etc/firebuild.conf.
 * */
static void parse_cfg_file(libconfig::Config *cfg, const char *custom_cfg_file) {
  // we fall back to global configuration file
  std::string cfg_file(GLOBAL_CONFIG);
  if (custom_cfg_file != NULL) {
    cfg_file = std::string(custom_cfg_file);
  } else {
    char *homedir = getenv("HOME");
    if (homedir != NULL) {
      std::string user_cfg_file = homedir + std::string("/" USER_CONFIG);
      int cfg_fd = open(user_cfg_file.c_str(), O_RDONLY);
      if (cfg_fd != -1) {
        // fall back to private config file
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
 *  E.g. str = "processes.blacklist += \"myapp\""
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
        std::string x_str = x;
        adding = x_str;
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
          std::string item_str = item;
          std::string x_str = x;
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
        std::string x_str = x;
        adding = x_str;
        break;
      }
      default:
        std::cerr << "This type is not supported" << std::endl;
        exit(EXIT_FAILURE);
    }
  }

  delete mini_config;
}

void read_config(libconfig::Config *cfg, const char *custom_cfg_file,
                 const std::list<std::string> &config_strings) {
  parse_cfg_file(cfg, custom_cfg_file);
  for (auto s : config_strings) {
    modify_config(cfg, s);
  }

  if (FB_DEBUGGING(FB_DEBUG_CONFIG)) {
    fprintf(stderr, "--- Config:\n");
    cfg->write(stderr);
    fprintf(stderr, "--- End of config.\n");
  }
}

}  // namespace firebuild
