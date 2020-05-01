/* Copyright (c) 2019 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */


#ifndef FIREBUILD_EXECEDPROCESSPARAMETERS_H_
#define FIREBUILD_EXECEDPROCESSPARAMETERS_H_

#include <string>
#include <vector>

#include "firebuild/file_fd.h"

namespace firebuild {

/**
 * A thin class representing a process by its command line parameters, inherited file descriptors
 * (and later perhaps environment variables too).
 */
class ExecedProcessParameters {
 public:
  ExecedProcessParameters();
  ExecedProcessParameters(std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds);
  bool operator == (ExecedProcessParameters const &pp) const;

  std::vector<std::string>& argv() {return argv_;}
  const std::vector<std::string>& argv() const {return argv_;}
  std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds() {return fds_;}

  void set_sh_c_command(const std::string&);

 private:
  std::vector<std::string> argv_;
  /// File descriptor states intherited from parent
  std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds_;
  // TODO(egmont) add envp ?
};

inline bool ExecedProcessParameters::operator == (ExecedProcessParameters const &pp) const {
  return (pp.argv() == argv());
}

std::string to_string(ExecedProcessParameters const&);

}  // namespace firebuild
#endif  // FIREBUILD_EXECEDPROCESSPARAMETERS_H_
