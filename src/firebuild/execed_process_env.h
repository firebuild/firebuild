/* Copyright (c) 2019 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */


#ifndef FIREBUILD_EXECED_PROCESS_ENV_H_
#define FIREBUILD_EXECED_PROCESS_ENV_H_

#include <memory>
#include <string>
#include <vector>

#include "firebuild/file_fd.h"

namespace firebuild {

/**
 * A process' inherited environment, command line parameters and file descriptors
 * (and later perhaps the environment variables too).
 */
class ExecedProcessEnv {
 public:
  ExecedProcessEnv();
  explicit ExecedProcessEnv(std::unique_ptr<std::vector<std::shared_ptr<FileFD>>> fds);
  bool operator == (ExecedProcessEnv const &pp) const;

  std::vector<std::string>& argv() {return argv_;}
  const std::vector<std::string>& argv() const {return argv_;}
  std::unique_ptr<std::vector<std::shared_ptr<FileFD>>> fds() {return std::move(fds_);}

  void set_sh_c_command(const std::string&);

 private:
  std::vector<std::string> argv_;
  /// File descriptor states intherited from parent
  std::unique_ptr<std::vector<std::shared_ptr<FileFD>>> fds_;
  // TODO(egmont) add envp ?
};

inline bool ExecedProcessEnv::operator == (ExecedProcessEnv const &pp) const {
  return (pp.argv() == argv());
}

std::string to_string(ExecedProcessEnv const&);

}  // namespace firebuild
#endif  // FIREBUILD_EXECED_PROCESS_ENV_H_
