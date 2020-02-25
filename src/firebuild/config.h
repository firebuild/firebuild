/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_CONFIG_H_
#define FIREBUILD_CONFIG_H_

#include <list>

#include <libconfig.h++>

namespace firebuild {

void read_config(libconfig::Config *cfg, const char *custom_cfg_file, const std::list<std::string>& config_strings);

}  // namespace firebuild
#endif  // FIREBUILD_CONFIG_H_
