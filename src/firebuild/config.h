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

extern cstring_view_array ignore_locations;
extern cstring_view_array system_locations;
extern ExeMatcher* dont_shortcut_matcher;
extern ExeMatcher* dont_intercept_matcher;
extern ExeMatcher* skip_cache_matcher;

/** Store results of processes consuming more CPU time (system + user) in microseconds than this. */
extern int64_t min_cpu_time_u;

/**
 * Give up after shortcut_tries and run the process without shortcutting it.
 * Value of 0 means trying all candidates.
 */
extern int shortcut_tries;

/** Enabled quirks represented as flags. See "quirks" in etc/firebuild.conf. */
extern int quirks;
#define FB_QUIRK_IGNORE_TMP_LISTING 0x01
#define FB_QUIRK_LTO_WRAPPER        0x02

void read_config(libconfig::Config *cfg, const char *custom_cfg_file,
                 const std::list<std::string>& config_strings);

}  /* namespace firebuild */
#endif  // FIREBUILD_CONFIG_H_
