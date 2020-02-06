/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/file_usage_db.h"

namespace firebuild {

FileUsageDB::~FileUsageDB() {
  for (auto pair : db_) {
    delete(pair.second);
  }
}

}  // namespace firebuild
