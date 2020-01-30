/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_DEBUG_H_
#define FIREBUILD_DEBUG_H_

#include <string>
#include <vector>

namespace firebuild {

/** Print error message */
void fb_error(const std::string &msg);

/**
 * Print debug message if debug level is at least lvl
 */
#define FB_DEBUG(lvl, msg) if (lvl <= firebuild::debug_level) \
    firebuild::fb_debug(msg)

void fb_debug(const std::string &msg);

/** current debugging level */
extern int debug_level;

std::string pretty_print_string(const std::string& str);

std::string pretty_print_array(const std::vector<std::string>& arr,
                               const std::string& sep = ", ");

/** Get a human-readable timestamp according to local time. */
std::string pretty_print_timestamp();

}  // namespace firebuild
#endif  // FIREBUILD_DEBUG_H_
