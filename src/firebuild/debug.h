/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_DEBUG_H_
#define FIREBUILD_DEBUG_H_

#include <string>
#include <vector>

namespace firebuild {

/** Print error message */
void fb_error(const std::string &msg);

/** Possible debug flags. Keep in sync with debug.cc! */
enum {
  /* Firebuild's configuration */
  FB_DEBUG_CONFIG       = 1 << 0,
  /* Events with one process, e.g. shortcut, exit */
  FB_DEBUG_PROC         = 1 << 1,
  /* How processes are organized into ProcTree */
  FB_DEBUG_PROCTREE     = 1 << 2,
  /* Communication */
  FB_DEBUG_COMM         = 1 << 3,
  /* File system */
  FB_DEBUG_FS           = 1 << 4,
  /* Checksum computation */
  FB_DEBUG_HASH         = 1 << 5,
  /* The data stored in the cache */
  FB_DEBUG_CACHE        = 1 << 6,
  /* Placing in / retrieving from the cache */
  FB_DEBUG_CACHING      = 1 << 7,
  /* Shortcutting */
  FB_DEBUG_SHORTCUT     = 1 << 8,
};

/**
 * Test if debugging this kind of events is enabled.
 */
#define FB_DEBUGGING(flag) (firebuild::debug_flags & flag)

/**
 * Print debug message if the given debug flag is enabled.
 */
#define FB_DEBUG(flag, msg) if (FB_DEBUGGING(flag)) \
    firebuild::fb_debug(msg)

void fb_debug(const std::string &msg);

/** Current debugging flags */
extern int32_t debug_flags;

int32_t parse_debug_flags(const std::string& str);

std::string pretty_print_string(const std::string& str);

std::string pretty_print_array(const std::vector<std::string>& arr,
                               const std::string& sep = ", ");

/** Get a human-readable timestamp according to local time. */
std::string pretty_print_timestamp();

}  // namespace firebuild
#endif  // FIREBUILD_DEBUG_H_
