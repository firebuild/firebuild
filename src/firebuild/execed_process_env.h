/* Copyright (c) 2019 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */


#ifndef FIREBUILD_EXECED_PROCESS_ENV_H_
#define FIREBUILD_EXECED_PROCESS_ENV_H_

#include <memory>
#include <string>
#include <vector>

#include "firebuild/file_fd.h"

namespace firebuild {

typedef enum {
  LAUNCH_TYPE_SYSTEM,
  LAUNCH_TYPE_POPEN,
  LAUNCH_TYPE_OTHER
} LaunchType;

/**
 * A process' inherited environment, command line parameters and file descriptors,
 * file actions to be executed on startup (for posix_spawn'ed children),
 * (and later perhaps the environment variables too).
 */
class ExecedProcessEnv {
 public:
  ExecedProcessEnv();
  explicit ExecedProcessEnv(std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds,
                            std::vector<std::string> file_actions = std::vector<std::string>());

  std::vector<std::string>& argv() {return argv_;}
  const std::vector<std::string>& argv() const {return argv_;}
  void set_argv(const std::vector<std::string>& argv) {argv_ = argv;}
  std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds() {return fds_;}
  std::shared_ptr<std::vector<std::string>> file_actions() {return file_actions_;}
  void set_launch_type(LaunchType value) {launch_type_ = value;}
  LaunchType launch_type() const {return launch_type_;}

  void set_sh_c_command(const std::string&);

 private:
  std::vector<std::string> argv_;
  /// Whether it's launched via system() or popen() or other
  LaunchType launch_type_;
  /// File descriptor states intherited from parent
  std::shared_ptr<std::vector<std::shared_ptr<FileFD>>> fds_;
  /// In case the process is started via a posix_spawn[p]() with a file_actions parameter:
  /// File operations to be executed, along with the usual administration of file events,
  /// as soon as the child appears.
  std::shared_ptr<std::vector<std::string>> file_actions_;
  // TODO(egmont) add envp ?

  DISALLOW_COPY_AND_ASSIGN(ExecedProcessEnv);
};

std::string to_string(ExecedProcessEnv const&);

}  // namespace firebuild
#endif  // FIREBUILD_EXECED_PROCESS_ENV_H_
