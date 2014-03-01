/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/FileDB.h"

namespace firebuild {

FileDB::~FileDB() {
  for (auto it = this->begin(); it != this->end(); ++it) {
    delete(it->second);
  }
}

}  // namespace firebuild
