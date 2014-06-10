/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/FileDB.h"

namespace firebuild {

FileDB::~FileDB() {
  for (auto pair : db_) {
    delete(pair.second);
  }
}

}  // namespace firebuild
