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
extern ExeMatcher* dont_intercept_matcher;
extern ExeMatcher* skip_cache_matcher;
/** Store results of processes consuming more CPU time (system + user) in microseconds than this. */
extern int64_t min_cpu_time_u;

void read_config(libconfig::Config *cfg, const char *custom_cfg_file,
                 const std::list<std::string>& config_strings);

}  /* namespace firebuild */
#endif  // FIREBUILD_CONFIG_H_
