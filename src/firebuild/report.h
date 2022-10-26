/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 */

#ifndef FIREBUILD_REPORT_H_
#define FIREBUILD_REPORT_H_

#include <string>

namespace firebuild {

class Report {
 public:
  /**
   * Write report to specified file
   *
   * @param html_filename report file to be written
   * @param datadir report template's location
   * TODO(rbalint) error handling
   */
  static void write(const std::string &html_filename, const std::string &datadir);
};

}  /* namespace firebuild */

#endif  // FIREBUILD_REPORT_H_
