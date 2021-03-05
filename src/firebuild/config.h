/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

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

extern std::vector<const FileName*> *ignore_locations;
extern ExeMatcher* dont_shortcut_matcher;
extern ExeMatcher* skip_cache_matcher;

void read_config(libconfig::Config *cfg, const char *custom_cfg_file,
                 const std::list<std::string>& config_strings);

}  // namespace firebuild
#endif  // FIREBUILD_CONFIG_H_
