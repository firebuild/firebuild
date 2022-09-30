/* Copyright (c) 2022 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_REPORT_H_
#define FIREBUILD_REPORT_H_

#include <string>

namespace firebuild {

class Report {
 public:
  static void write(const std::string &html_filename, const std::string &datadir);
};

}  /* namespace firebuild */

#endif  // FIREBUILD_REPORT_H_
