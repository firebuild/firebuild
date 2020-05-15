/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_UTILS_H_
#define FIREBUILD_UTILS_H_

#include <string>
#include <unordered_map>
#include <libconfig.h++>

#include "firebuild/cache.h"
#include "firebuild/multi_cache.h"
#include "firebuild/execed_process.h"
#include "firebuild/hash.h"
#include "firebuild/fb-cache.pb.h"

namespace firebuild {

bool path_begins_with(const std::string& path, const std::string& prefix);

}  // namespace firebuild
#endif  // FIREBUILD_UTILS_H_
