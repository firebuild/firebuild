/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 *
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 * Modification and redistribution are permitted, but commercial use of derivative
 * works is subject to the same requirements of this license
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#include "firebuild/execed_process.h"

#include <cinttypes>
#include <memory>

#include <libconfig.h++>

#include "common/platform.h"
#include "firebuild/config.h"
#include "firebuild/execed_process_cacher.h"
#include "firebuild/forked_process.h"
#ifdef __APPLE__
#include "firebuild/hash_cache.h"
#endif
#include "firebuild/options.h"
#include "firebuild/process_debug_suppressor.h"
#include "firebuild/process_tree.h"
#include "firebuild/utils.h"

namespace firebuild {

ExecedProcess::ExecedProcess(const int pid, const int ppid,
                             const FileName *initial_wd,
                             const FileName *executable,
                             const FileName *executed_path,
                             char* original_executed_path,
                             const std::vector<std::string>& args,
                             const std::vector<std::string>& env_vars,
                             const std::vector<const FileName*>& libs,
                             const mode_t umask,
                             Process * parent,
                             const bool debug_suppressed,
                             std::vector<std::shared_ptr<FileFD>>* fds)
    : Process(pid, ppid, parent ? parent->exec_count() + 1 : 1, initial_wd, umask, parent,
              fds, debug_suppressed),
      maybe_shortcutable_ancestor_(
          (parent && parent->exec_point()) ? parent->exec_point()->closest_shortcut_point()
          : nullptr),
      initial_wd_(initial_wd), wds_(), failed_wds_(), args_(args), env_vars_(env_vars),
      executable_(executable), executed_path_(executed_path),
      original_executed_path_(original_executed_path),
      libs_(libs), file_usages_(), created_pipes_() {
  TRACKX(FB_DEBUG_PROC, 0, 1, Process, this,
         "pid=%d, ppid=%d, initial_wd=%s, executable=%s, umask=%03o, parent=%s",
         pid, ppid, D(initial_wd), D(executable), umask, D(parent));

  if (parent) {
    assert(parent->state() == FB_PROC_TERMINATED);
    /* add as exec child of parent */
    fork_point_ = parent->fork_point();
    parent->set_exec_pending(false);
    parent->reset_file_fd_pipe_refs();
    parent->set_exec_child(this);
  }
}
ExecedProcess* ExecedProcess::common_exec_ancestor(ExecedProcess* other) {
  TRACKX(FB_DEBUG_PROC, 0, 1, Process, this, "other=%s", D(other));
  tsl::hopscotch_set<ExecedProcess*> my_ancestors;
  tsl::hopscotch_set<ExecedProcess*> others_ancestors;
  ExecedProcess* curr_this = this;
  ExecedProcess* curr_other = other;
  /* Walk up on each branch in parallel collecting the visited nodes in two sets
   * to find the common ancestor when a newly visited node is in the other branch's set. */
  do {
    if (curr_this) {
      if (others_ancestors.find(curr_this) != others_ancestors.end()) {
        return curr_this;
      } else {
        my_ancestors.insert(curr_this);
        curr_this = curr_this->parent_exec_point();
      }
    }
    if (curr_other) {
      if (my_ancestors.find(curr_other) != my_ancestors.end()) {
        return curr_other;
      } else {
        others_ancestors.insert(curr_other);
        curr_other = curr_other->parent_exec_point();
      }
    }
  } while (curr_this || curr_other);
  assert(0 && "not reached");
  return nullptr;
}

void ExecedProcess::set_parent(Process *parent) {
  /* set_parent() is called only on processes which are created by posix_spawn(). */
  assert(parent);
  ExecedProcess* parent_exec_point = parent->exec_point();
  assert(parent_exec_point);
  Process::set_parent(parent);
  fork_point_ = parent->fork_point();
  maybe_shortcutable_ancestor_ = parent_exec_point->closest_shortcut_point();
}

void ExecedProcess::initialize() {
  TRACKX(FB_DEBUG_PROC, 0, 1, Process, this, "");

  /* Propagate the opening of the executable and libraries upwards as
   * regular file open events. */
  if (parent_exec_point()) {
    FileUsageUpdate exe_update =
        FileUsageUpdate::get_from_open_params(executable(), O_RDONLY, 0, 0, false);
    parent_exec_point()->register_file_usage_update(executable(), exe_update);
    for (const auto& lib : libs_) {
#ifdef __APPLE__
      const int error = hash_cache->get_statinfo(lib, nullptr, nullptr) ? 0 : ENOENT;
#else
      const int error = 0;
#endif
      FileUsageUpdate lib_update =
          FileUsageUpdate::get_from_open_params(lib, O_RDONLY, 0, error, false);
      parent_exec_point()->register_file_usage_update(lib, lib_update);
    }
  }

  /* Find the inherited files.
   * Group them according to the open file description they point to.
   * E.g. if fd 1 & 2 are dups of each other, but fd 3 is the same file opened by name separately
   * then build up [[1, 2], [3]].
   * The outer list (according to the lowest fd) and the inner lists are all sorted. */
  std::vector<inherited_file_t> inherited_files;
  /* This iterates over the fds in increasing order. */
  int fds_size = fds()->size();
  for (int fd = 0; fd < fds_size; fd++) {
    const FileFD* file_fd = get_fd(fd);
    if (!file_fd) {
      continue;
    }
    bool found = false;
    for (inherited_file_t& inherited_file : inherited_files) {
      if (get_fd(inherited_file.fds[0])->fdcmp(*file_fd) == 0) {
        inherited_file.fds.push_back(fd);
        found = true;
        break;
      }
    }
    if (!found) {
      inherited_file_t inherited_file;
      inherited_file.type = file_fd->type();
      assert(inherited_file.type != FD_UNINITIALIZED);
      inherited_file.fds.push_back(fd);
      inherited_file.filename = file_fd->filename();
      inherited_file.flags = file_fd->flags();
      inherited_files.push_back(inherited_file);
    }
  }

  for (inherited_file_t& inherited_file : inherited_files) {
    if (inherited_file.type == FD_FILE) {
      /* Remember the file size and seek offset. */
      FileFD *file_fd = (*fds())[inherited_file.fds[0]].get();
      struct stat64 st;
      if (stat64(file_fd->filename()->c_str(), &st) < 0) {
        disable_shortcutting_only_this("Failed to stat inherited file");
      } else if (S_ISREG(st.st_mode) && is_write(file_fd->flags())) {
        /* Assume that the file is seeked to the end. Otherwise the interceptor will
         * report the offset back. */
        inherited_file.start_offset = st.st_size;
      }
    }
  }

  set_inherited_files(inherited_files);

  if (FB_DEBUGGING(FB_DEBUG_PROC)) {
    FB_DEBUG(FB_DEBUG_PROC, "Client-side fds are:");
    for (const inherited_file_t& inherited_file : inherited_files) {
      std::string arr = "  type=" + std::string(fd_type_to_string(inherited_file.type)) + " [";
      bool add_sep = false;
      for (int fd : inherited_file.fds) {
        if (add_sep) {
          arr += ", ";
        }
        add_sep = true;
        arr += std::to_string(fd);
      }
      arr += "]";
      FB_DEBUG(FB_DEBUG_PROC, arr);
    }
  }
}

void ExecedProcess::set_on_finalized_ack(int id, int fd) {
  fork_point()->set_on_finalized_ack(id, fd);
}

bool ExecedProcess::been_waited_for() const {
  return fork_point()->been_waited_for();
}

void ExecedProcess::set_been_waited_for() {
  fork_point()->set_been_waited_for();
}

void ExecedProcess::resource_usage(const int64_t utime_u, const int64_t stime_u) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this,
         "utime_u=%" PRId64 ", stime_u=%" PRId64, utime_u, stime_u);

  /* store resource usage for this process */
  Process::resource_usage(utime_u, stime_u);
}

void ExecedProcess::do_finalize() {
  ProcessDebugSuppressor debug_suppressor(this);
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "");

  close_fds();

  /* store data for shortcutting */
  if (!was_shortcut() && can_shortcut() && fork_point()->exit_status() != -1
      && aggr_cpu_time_u() >= min_cpu_time_u) {
    execed_process_cacher->store(this);
  }

  /* Propagate resource usage. */
  if (parent_exec_point()) {
    parent_exec_point()->add_children_cpu_time_u(aggr_cpu_time_u());
    parent_exec_point()->add_shortcut_cpu_time_ms(shortcut_cpu_time_ms());
  }

  /* Call the base class's method */
  Process::do_finalize();

  for (const auto& pipe : created_pipes_) {
    pipe->finish();
  }
  proc_tree->QueueExecProcForGC(this);
}

bool ExecedProcess::register_file_usage_update(const FileName *name,
                                        const FileUsageUpdate& update) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "name=%s, update=%s", D(name), D(update));

  /* What the FileUsage was before the update, on the previous (descendant) level. The initial value
   * should differ from any valid value, including NULL which is valid too (indicating no prior
   * knowledge about that file), to allow to detect if we're about the perform the same update
   * operation as we did on the previous (descendant) level. It should also differ from fu_new's
   * initial value, to allow to detect if the update didn't change anything. */
  const FileUsage *fu_old = reinterpret_cast<FileUsage *>(-1);
  /* What the FileUsage became after the update, on the previous (descendant) level. */
  const FileUsage *fu_new = nullptr;

  if (name->is_in_ignore_location()) {
    FB_DEBUG(FB_DEBUG_FS, "Ignoring file usage: " + d(name));
    return true;
  }

  bool propagated = false;
  ExecedProcess *proc = this;
  if (!proc->can_shortcut_ && !Options::generate_report()) {
    /* Register at the first shortcutable ancestor instead. */
    proc = proc->next_shortcutable_ancestor();
    propagated = true;
  }
  if (!proc) {
    return true;
  }

  /* Register (and bubble up) what we implicitly got to know about the parent directory. This call
   * will in turn call back to us, but it's not a recursion since 'update' will then have
   * parent_type_ == DONTKNOW. */
  if (update.parent_type() != DONTKNOW) {
    proc->register_parent_directory(name, update.parent_type());
  }

  /* Bubble up to the root, but abort the loop if at a certain level we didn't do anything (fu_new
   * became the same as fu_old was) because then continuing to bubble up wouldn't do anything new
   * either. */
  while (proc && fu_new != fu_old) {
    ProcessDebugSuppressor debug_suppressor(proc == this ? nullptr : proc);
    const FileUsage *fu = nullptr;

    auto it = proc->file_usages_.find(name);
    if (it != proc->file_usages_.end()) {
      fu = it->second;
    }

    if (fu == fu_old) {
      /* Quick path: We need to do the same as we did on the previous level (fu and fu_old can be
       * NULL or non-NULL, the same quick path logic applies). The new value is fu_new (which can be
       * the same as fu_old or can differ, again, the same quick path logic applies for both cases),
       * just as it was on the previous level. Nothing to do here. */
    } else {
      /* Need to merge "update" into "fu". */
      fu_old = fu;
      if (!fu) {
        fu = FileUsage::Get(DONTKNOW);
      } else {
        if (fu->generation() != update.generation()
            && fu->generation() + 1 != update.generation()) {
          /* If all file changes were performed by descendants then the generation updates should
           * always be incremented by one. Otherwise the file could have been changed outside of the
           * process's subtree wich makes the process not shortcutable. */
          proc->disable_shortcutting_only_this(
              Options::generate_report()
              ? deduplicated_string("A parallel process modified " + d(name)).c_str()
              : "A parallel process modified the file");
          /* Still bubble up to the root because an ancestor may still be shortcutable and also
           * been updated with the parallel change. */
          propagated = true;
          proc = proc->next_shortcutable_ancestor();
          if (proc) {
            /* There is no merged file usage, it can't be carried in this loop. Recurse instead. */
            return proc->register_file_usage_update(name, update);
          } else {
            /* No process to bubble up to, file usage registration in finshed. */
            return true;
          }
        }
      }
      /* Note: This can update "update" if some value is computed now and cached there. In that
       * case, in the next iteration of the loop "update" will already contain this value. */
      fu_new = fu->merge(update, propagated);
      if (!fu_new) {
        if (FB_DEBUGGING(FB_DEBUG_FS) || Options::generate_report()) {
          std::string reason("Could not merge " + d(name) + " file usage " + d(fu) + " with "
                             + d(update));
          FB_DEBUG(FB_DEBUG_FS, reason);
          disable_shortcutting_bubble_up(
              "Could not register unsupported file usage combination:", reason);
        } else {
          disable_shortcutting_bubble_up("Could not register unsupported file usage combination");
        }
        return false;
      }
    }

    if (it != proc->file_usages_.end()) {
      /* Update the previous value. */
      it.value() = fu_new;
    } else {
      /* Insert a new value. */
      proc->file_usages_[name] = fu_new;
    }

    propagated = true;
    proc = proc->next_shortcutable_ancestor();
  }
  return true;
}

bool ExecedProcess::register_parent_directory(const FileName *name,
                                              FileType parent_type) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "name=%s", D(name));

  const FileName* const parent_dir = name->parent_dir();

  if (!parent_dir) {
    return false;
  } else if (parent_dir->length() == 1 && parent_dir->c_str()[0] == '/') {
    /* don't bother registering "/" */
    return true;
  }

  FileUsageUpdate update(parent_dir, parent_type);
  assert(update.parent_type() == DONTKNOW);
  return register_file_usage_update(parent_dir, update);
}

bool ExecedProcess::shortcut(std::vector<int> *fds_appended_to) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "");

  if (can_shortcut()) {
    return execed_process_cacher->shortcut(this, fds_appended_to);
  } else {
    FB_DEBUG(FB_DEBUG_SHORTCUT, "┌─");
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│ Shortcutting disabled:");
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   exe = " + d(executable()));
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   arg = " + d(args()));
    /* FB_DEBUG(FB_DEBUG_SHORTCUT, "│   env = " + d(env_vars())); */
    FB_DEBUG(FB_DEBUG_SHORTCUT, "└─");
    return false;
  }
}

const char* ExecedProcess::reason_with_fd(const char* reason, const int fd) const {
  return deduplicated_string(std::string(reason) + " fd: " + d(fd)
                             + ((get_fd(fd) && get_fd(fd)->filename())
                                ? (std::string(", name: ") + get_fd(fd)->filename()->c_str())
                                : "")).c_str();
}

void ExecedProcess::disable_shortcutting_bubble_up_to_excl(
    ExecedProcess *stop,
    const char* reason,
    const ExecedProcess *p,
    ExecedProcess *shortcutable_ancestor,
    bool shortcutable_ancestor_is_set) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "stop=%s, reason=%s, source=%s",
         D(stop), D(reason), D(p));

  if (this == stop) {
    return;
  }
  if (p == NULL) {
    p = this;
  }
  disable_shortcutting_only_this(reason, p);
  if (next_shortcutable_ancestor() == nullptr) {
    /* Shortcutting is already disabled for all transitive exec parents. */
    return;
  }
  if (!shortcutable_ancestor_is_set) {
    /* Move maybe_shortcutable_ancestor_ only upwards. */
    shortcutable_ancestor = stop;
    while (shortcutable_ancestor != nullptr && !shortcutable_ancestor->can_shortcut()) {
      shortcutable_ancestor = shortcutable_ancestor->maybe_shortcutable_ancestor_;
    }
    shortcutable_ancestor_is_set = true;
  }
  maybe_shortcutable_ancestor_ = shortcutable_ancestor;

  if (parent_exec_point()) {
    parent_exec_point()->disable_shortcutting_bubble_up_to_excl(
        stop, reason, p, shortcutable_ancestor, shortcutable_ancestor_is_set);
  }
}

void ExecedProcess::disable_shortcutting_bubble_up_to_excl(
    ExecedProcess *stop,
    const char* reason,
    const int fd,
    const ExecedProcess *p,
    ExecedProcess *shortcutable_ancestor,
    bool shortcutable_ancestor_is_set) {
  disable_shortcutting_bubble_up_to_excl(
      stop,
      Options::generate_report() ? reason_with_fd(reason, fd) : reason,
      p, shortcutable_ancestor, shortcutable_ancestor_is_set);
  FB_DEBUG(FB_DEBUG_PROC, "fd: " + d(fd));
}
void ExecedProcess::disable_shortcutting_bubble_up(const char* reason,
                                                   const ExecedProcess *p) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "reason=%s, source=%s", D(reason), D(p));

  disable_shortcutting_bubble_up_to_excl(NULL, reason, p);
}

void ExecedProcess::disable_shortcutting_bubble_up(const char* reason,
                                                   const int fd,
                                                   const ExecedProcess *p) {
  disable_shortcutting_bubble_up(
      Options::generate_report() ? reason_with_fd(reason, fd) : reason, p);
  FB_DEBUG(FB_DEBUG_PROC, "fd: " + d(fd));
}

void ExecedProcess::disable_shortcutting_bubble_up(const char* reason,
                                                   const FileName& file,
                                                   const ExecedProcess *p) {
  disable_shortcutting_bubble_up(
      Options::generate_report() ? deduplicated_string(std::string(reason) + " file: "
                                                       + d(file)).c_str()
      : reason, p);
  FB_DEBUG(FB_DEBUG_PROC, "file: " + d(file));
}

void ExecedProcess::disable_shortcutting_bubble_up(const char* reason,
                                                   const std::string& str,
                                                   const ExecedProcess *p) {
  disable_shortcutting_bubble_up(
      Options::generate_report() ? deduplicated_string(std::string(reason) + " " + d(str)).c_str()
      : reason, p);
  FB_DEBUG(FB_DEBUG_PROC, d(str));
}

void ExecedProcess::disable_shortcutting_only_this(const char* reason,
                                                   const ExecedProcess *p) {
  ProcessDebugSuppressor debug_suppressor(this);
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "reason=%s, source=%s", D(reason), D(p));

  if (can_shortcut_) {
    can_shortcut_ = false;
    assert(cant_shortcut_reason_ == nullptr);
    cant_shortcut_reason_ = reason;
    assert_null(cant_shortcut_proc_);
    cant_shortcut_proc_ = p ? p : this;
    FB_DEBUG(FB_DEBUG_PROC, "Command " + d(executable_->c_str())
             + " can't be short-cut due to: " + reason + ", " + d(this));

    for (const inherited_file_t& inherited_file : inherited_files_) {
      if (inherited_file.recorder) {
        assert(inherited_file.type == FD_PIPE_OUT);
        inherited_file.recorder->deactivate();
      }
    }
  }
}

std::string ExecedProcess::args_to_short_string() const {
  const int max_len = 65;
  if (args().size() == 0) {
    return "";
  }
  size_t slash_pos = args()[0].rfind('/');
  std::string str;
  if (slash_pos == std::string::npos) {
    str = args()[0];
  } else {
    str = args()[0].substr(slash_pos + 1);
  }
  for (size_t i = 1; i < args().size(); i++) {
    str += " ";
    str += args()[i];
  }
  if (str.length() <= max_len) {
    return str;
  } else {
    const int one_run = max_len / 2 - 5;
    return str.substr(0, one_run) + "[...]" + str.substr(str.length() - one_run);
  }
}

std::string ExecedProcess::d_internal(const int level) const {
  if (level > 0) {
    /* brief */
    return Process::d_internal(level);
  } else {
    /* verbose */
    return "{ExecedProcess " + pid_and_exec_count() + ", " + state_string() + ", " +
        d(args_to_short_string()) + ", fds=" + d(fds(), level + 1) + "}";
  }
}

ExecedProcess::~ExecedProcess() {
  TRACKX(FB_DEBUG_PROC, 1, 0, Process, this, "");

  if (original_executed_path_ != executed_path_->c_str()) {
    free(original_executed_path_);
  }
  execed_process_cacher->erase_fingerprint(this);
}

}  /* namespace firebuild */
