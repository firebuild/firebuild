/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_CONFIG_H_
#define FIREBUILD_CONFIG_H_

#include <list>
#include <string>

#include <libconfig.h++>

#include "common/firebuild_common.h"

namespace firebuild {

extern string_array ignore_locations;
void read_config(libconfig::Config *cfg, const char *custom_cfg_file,
                 const std::list<std::string>& config_strings);

}  // namespace firebuild
#endif  // FIREBUILD_CONFIG_H_
