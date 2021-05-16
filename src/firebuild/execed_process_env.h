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
  explicit ExecedProcessEnv(std::vector<std::shared_ptr<FileFD>>* fds);
  ~ExecedProcessEnv() {delete fds_;}

  std::vector<std::string>& argv() {return argv_;}
  const std::vector<std::string>& argv() const {return argv_;}
  void set_argv(const std::vector<std::string>& argv) {argv_ = argv;}
  void set_argv(const std::vector<std::string_view>& argv) {
    for (const auto& arg : argv) {
      argv_.push_back(std::string(arg) );
    }
  }
  std::vector<std::shared_ptr<FileFD>>* pop_fds() {
    std::vector<std::shared_ptr<FileFD>>* ret = fds_;
    fds_ = nullptr;
    return ret;
  }
  void set_launch_type(LaunchType value) {launch_type_ = value;}
  LaunchType launch_type() const {return launch_type_;}
  void set_type_flags(int type_flags) {type_flags_ = type_flags;}
  int type_flags() const {return type_flags_;}

  void set_sh_c_command(const std::string&);

 private:
  std::vector<std::string> argv_;
  /// Whether it's launched via system() or popen() or other
  LaunchType launch_type_;
  /// popen(command, type)'s type encoded as O_WRONLY | O_RDONLY | O_CLOEXEC flags
  int type_flags_;
  /// File descriptor states intherited from parent
  std::vector<std::shared_ptr<FileFD>>* fds_;
  // TODO(egmont) add envp ?

  DISALLOW_COPY_AND_ASSIGN(ExecedProcessEnv);
};

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const ExecedProcessEnv& env, const int level = 0);
std::string d(const ExecedProcessEnv *env, const int level = 0);

}  // namespace firebuild
#endif  // FIREBUILD_EXECED_PROCESS_ENV_H_
