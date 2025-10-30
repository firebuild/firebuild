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


#ifndef FIREBUILD_EXECED_PROCESS_H_
#define FIREBUILD_EXECED_PROCESS_H_

#include <tsl/hopscotch_map.h>
#include <tsl/hopscotch_set.h>

#include <cassert>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "firebuild/file_info.h"
#include "firebuild/file_name.h"
#include "firebuild/file_usage.h"
#include "firebuild/file_usage_update.h"
#include "firebuild/pipe.h"
#include "firebuild/pipe_recorder.h"
#include "firebuild/process.h"
#include "firebuild/cxx_lang_utils.h"
#include "firebuild/debug.h"

namespace firebuild {

class ExecedProcessCacher;

/**
 * Represents one open file description that this process inherited, along with the list of
 * corresponding file descriptors. (An open file description might have multiple file descriptors,
 * as per dup() and friends. They are stored in ascending order. There's at least one fd.)
 *
 * The structure always refers to how things were when the process started,
 * it isn't modified later as the process does various things with its file descriptors.
 *
 * Accordingly, for pipes, it does not hold a pointer to the Pipe object, since that one might go
 * away while we still need to keep this structure.
 */
typedef struct inherited_file_ {
  /* Type. */
  fd_type type {FD_UNINITIALIZED};
  /* The client-side file descriptor numbers, sorted */
  std::vector<int> fds {};
  /* For FD_PIPE_OUT only: The recorder of the traffic, as seen from this exec point */
  std::shared_ptr<PipeRecorder> recorder {};
  /* For type FD_FILE. */
  const FileName *filename {};
  /* Flags. We need to know for regular files if they're opened for writing. */
  int flags {-1};
  /* For writable FD_FILE: If the O_APPEND flag is not set then it's the seek offset when the
   * process started up. If O_APPEND is set then it's the file size as of that time. That is, in
   * either case, assuming no other process writes to the file, and assuming that this process does
   * sequential writes only, this is the offset from where this process writes its data. */
  ssize_t start_offset {-1};
} inherited_file_t;

class ExecedProcess : public Process {
 public:
  explicit ExecedProcess(const int pid, const int ppid, const FileName *initial_wd,
                         const FileName *executable, const FileName *executed_path,
                         char* original_executed_path,
                         const std::vector<std::string>& args,
                         const std::vector<std::string>& env_vars,
                         const std::vector<const FileName*>& libs,
                         const mode_t umask,
                         Process * parent,
                         const bool debug_suppressed,
                         std::vector<std::shared_ptr<FileFD>>* fds);
  virtual ~ExecedProcess();
  virtual bool exec_started() const {return true;}
  ExecedProcess* exec_point() {return this;}
  const ExecedProcess* exec_point() const {return this;}
  ExecedProcess* common_exec_ancestor(ExecedProcess* other);
  ForkedProcess* fork_point() {return fork_point_;}
  const ForkedProcess* fork_point() const {return fork_point_;}
  void set_parent(Process *parent);
  /**
   * Set jobserver fds if they are reasonably small and fit in the member's type.
   */
  void maybe_set_jobserver_fds(int fd_r, int fd_w) {
    if (fd_w >= 0 && fd_r <= INT16_MAX) {
      jobserver_fd_r_ = fd_r;
    }
    if (fd_w >= 0 && fd_w <= INT16_MAX) {
      jobserver_fd_w_ = fd_w;
    }
  }
  int jobserver_fd_r() const {return jobserver_fd_r_;}
  int jobserver_fd_w() const {return jobserver_fd_w_;}
  void set_jobserver_fifo(const char *fifo) {jobserver_fifo_ = FileName::Get(fifo);}
  const FileName* jobserver_fifo() const {return jobserver_fifo_;}
  bool been_waited_for() const;
  void set_been_waited_for();
  void add_utime_u(int64_t t) {utime_u_ += t;}
  int64_t utime_u() const {return utime_u_;}
  void add_stime_u(int64_t t) {stime_u_ += t;}
  int64_t stime_u() const {return stime_u_;}
  int64_t cpu_time_u() const {return utime_u_ + stime_u_;}
  void add_children_cpu_time_u(const int64_t t) {children_cpu_time_u_ += t;}
  void add_shortcut_cpu_time_ms(const int64_t t) {shortcut_cpu_time_ms_ += t;}
  int64_t shortcut_cpu_time_ms() const {return shortcut_cpu_time_ms_;}
  int64_t aggr_cpu_time_u() const {return cpu_time_u() + children_cpu_time_u_;}
  const FileName* initial_wd() const {return initial_wd_;}
  const tsl::hopscotch_set<const FileName*>& wds() const {return wds_;}
  const tsl::hopscotch_set<const FileName*>& wds() {return wds_;}
  const tsl::hopscotch_set<const FileName*>& failed_wds() const {return wds_;}
  tsl::hopscotch_set<const FileName*>& failed_wds() {return failed_wds_;}
  const std::vector<std::string>& args() const {return args_;}
  std::vector<std::string>& args() {return args_;}
  const std::vector<std::string>& env_vars() const {return env_vars_;}
  std::vector<std::string>& env_vars() {return env_vars_;}
  const FileName* executable() const {return executable_;}
  const FileName* executed_path() const {return executed_path_;}
  const char* original_executed_path() const {return original_executed_path_;}
  std::vector<const FileName*>& libs() {return libs_;}
  const std::vector<const FileName*>& libs() const {return libs_;}
  tsl::hopscotch_map<const FileName*, const FileUsage*>& file_usages() {return file_usages_;}
  const tsl::hopscotch_map<const FileName*, const FileUsage*>& file_usages() const {
    return file_usages_;
  }
  void do_finalize();
  void set_on_finalized_ack(int id, int fd);
  Process* exec_proc() const {return const_cast<ExecedProcess*>(this);}
  void resource_usage(const int64_t utime_u, const int64_t stime_u);

  /**
   * Initialization stuff that can only be done after placing the
   * ExecedProcess in the ProcessTree.
   */
  void initialize();
  /**
   * Registers a file operation described in "update" into the filename "name", and bubbles it up to
   * the root.
   *
   * "update" might contain some lazy bits that will be computed on demand.
   *
   * In some rare cases the filename within "update" might differ from "name", in that case the
   * filename mentioned in "update" is used to lazily figure out the required values (such as
   * checksum), but it is registered as if it belonged to the file mentioned in this method's "name"
   * parameter. Currently this trick is only used for a rename()'s source path.
   *
   * This method also registers the implicit parent directory and bubbles it up, as per the
   * information contained in "update".
   */
  bool register_file_usage_update(const FileName *name, const FileUsageUpdate& update)
      __attribute__((nonnull(2)));
  /**
   * Register that the parent (a.k.a. dirname) of the given path does (or does not) exist and is of
   * the given "type" (e.g. ISDIR, NOTEXIST), and bubbles it up to the root.
   */
  bool register_parent_directory(const FileName *name, FileType parent_type = ISDIR);
  void add_pipe(std::shared_ptr<Pipe> pipe) {created_pipes_.insert(pipe);}
  std::vector<inherited_file_t>& inherited_files() {return inherited_files_;}
  const std::vector<inherited_file_t>& inherited_files() const {return inherited_files_;}
  void set_inherited_files(std::vector<inherited_file_t> inherited_files)
      {inherited_files_ = inherited_files;}

  /**
   * Fail to change to a working directory
   */
  void handle_fail_wd(const char * const d) {
    failed_wds_.insert(FileName::Get(d));
  }
  /**
   * Record visited working directory
   */
  void add_wd(const FileName *d) {
    wds_.insert(d);
  }

  /** Returns if the process can be short-cut */
  bool can_shortcut() const {return can_shortcut_;}

  /** Reason for this process can't be short-cut */
  const char* cant_shortcut_reason() const {return cant_shortcut_reason_;}

  /** Process the event preventing short-cutting happened in */
  const Process* cant_shortcut_proc() const {return cant_shortcut_proc_;}

  /** Result of the shortcut attempt if the process can be shotcut. */
  void set_shortcut_result(const char* result) {shortcut_result_ = result;}

  /** Result of the shortcut attempt if the process can be shotcut. */
  const char* shortcut_result() const {return shortcut_result_;}

  /** Find and apply shortcut */
  bool shortcut(std::vector<int> *fds_appended_to);

  /**
   * This particular process can't be short-cut because it performed calls preventing that.
   * @param reason reason for can't being short-cut
   * @param p process the event preventing shortcutting happened in, or
   *     omitted for the current process
   */
  virtual void disable_shortcutting_only_this(const char* reason,
                                              const ExecedProcess *p = NULL);
  /**
   * Process and parents (transitively) up to (excluding) "stop" can't be short-cut because
   * it performed calls preventing that.
   * @param stop Stop before this process
   * @param reason reason for can't being short-cut
   * @param p process the event preventing shortcutting happened in, or
   *     omitted for the current process
   * @param shortcutable_ancestor this ancestor will be the nearest shortcutable ancestor
   *        for all visited execed processes after this call
   *        (when shortcutable_ancestor_is_set is true)
   * @param shortcutable_ancestor_is_set the shortcutable_ancestor is computed
   */
  void disable_shortcutting_bubble_up_to_excl(ExecedProcess *stop, const char* reason,
                                              const ExecedProcess *p = NULL,
                                              ExecedProcess *shortcutable_ancestor = nullptr,
                                              bool shortcutable_ancestor_is_set = false);
  void disable_shortcutting_bubble_up_to_excl(ExecedProcess *stop, const char* reason, int fd,
                                              const ExecedProcess *p = NULL,
                                              ExecedProcess *shortcutable_ancestor = nullptr,
                                              bool shortcutable_ancestor_is_set = false);
  /**
   * Process and parents (transitively) can't be short-cut because it performed
   * calls preventing that.
   * @param reason reason for can't being short-cut
   * @param p process the event preventing shortcutting happened in, or
   *     omitted for the current process
   */
  void disable_shortcutting_bubble_up(const char* reason, const ExecedProcess *p = NULL);
  void disable_shortcutting_bubble_up(const char* reason, const int fd,
                                      const ExecedProcess *p = NULL);
  void disable_shortcutting_bubble_up(const char* reason, const FileName& file,
                                      const ExecedProcess *p = NULL);
  void disable_shortcutting_bubble_up(const char* reason, const std::string& str,
                                      const ExecedProcess *p = NULL);

  bool was_shortcut() const {return was_shortcut_;}
  void set_was_shortcut(bool value) {was_shortcut_ = value;}

  void set_qemu_user_used(bool value) {qemu_user_used_ = value;}
  bool qemu_user_used() const {return qemu_user_used_;}

  /** For debugging, a short imprecise reminder of the command line. Omits the path to the
   * executable, and strips off the middle. Does not escape or quote. */
  std::string args_to_short_string() const;
  /* Member debugging method. Not to be called directly, call the global d(obj_or_ptr) instead.
   * level is the nesting level of objects calling each other's d(), bigger means less info to print.
   * See #431 for design and rationale. */
  virtual std::string d_internal(const int level = 0) const;

 private:
  bool can_shortcut_:1 = true;
  bool was_shortcut_:1 = false;
  bool qemu_user_used_:1 = false;
  int16_t jobserver_fd_r_ = -1;
  int16_t jobserver_fd_w_ = -1;
  /** If points to this (self), the process can be shortcut.
      Otherwise the process itself is not shortcutable, but the ancestor is, if the ancestor's
      maybe_shortcutable_ancestor points at itself, etc. */
  ForkedProcess *fork_point_ {};
  ExecedProcess * maybe_shortcutable_ancestor_;
  /** Sum of user time in microseconds for all forked but not exec()-ed children */
  int64_t utime_u_ = 0;
  /** Sum of system time in microseconds for all forked but not exec()-ed children */
  int64_t stime_u_ = 0;
  /**
   * Sum of user and system time in microseconds for all finalized exec()-ed children.
   * Shortcut processes are treated as if their CPU time was 0. */
  int64_t children_cpu_time_u_ = 0;
  /**
   * Aggregate CPU time saved by shortcutting in all transitive children and this process.
   * It becomes final when all exec()-ed children are finalized. */
  int64_t shortcut_cpu_time_ms_ {0};
  /** Directory the process exec()-started in */
  const FileName* initial_wd_;
  /** Working directories visited by the process and all fork()-children */
  tsl::hopscotch_set<const FileName*> wds_;
  /** Working directories the process and all fork()-children failed to chdir() to */
  tsl::hopscotch_set<const FileName*> failed_wds_;
  std::vector<std::string> args_;
  /** Environment variables in deterministic (sorted) order. */
  std::vector<std::string> env_vars_;
  /**
   * The executable running. In case of scripts this is the interpreter or in case of invoking
   * an executable via a symlink this is the executable the symlink points to. */
  const FileName* executable_;
  /**
   * The path executed converted to absolute and canonical form.
   * In case of scripts this is the script's name or in case of invoking executable via a symlink
   * this is the name of the symlink. */
  const FileName* executed_path_;
  /**
    * The path executed. In case of scripts this is the script's name or in case of invoking
    * executable via a symlink this is the name of the symlink.
    * May be not absolute nor canonical, like ./foo.
    * It may point to executed_path_.c_str() and in that case it should not be freed. */
  char* original_executed_path_;
  /**
   * DSO-s loaded by the linker at process startup, in the same order.
   * (DSO-s later loaded via dlopen(), and DSO-s of descendant processes are registered as regular
   * file open operations.) */
  std::vector<const FileName*> libs_;
  /** File usage per path for p and f. c. (t.) */
  tsl::hopscotch_map<const FileName*, const FileUsage*> file_usages_;
  /**
   * Pipes created by this process.
   */
  tsl::hopscotch_set<std::shared_ptr<Pipe>> created_pipes_ = {};
  /**
   * The files this process had at startup, grouped by "open file description".
   * Each such "open file description" might have multiple client-side file descriptors (see dup()
   * and friends), they are in sorted order. Also, this inherited_files_ array is sorted according
   * to the first (lowest) fd for each inherited file.
   */
  std::vector<inherited_file_t> inherited_files_ = {};
  void store_in_cache();
  ExecedProcess* next_shortcutable_ancestor() {
    if (maybe_shortcutable_ancestor_ == nullptr || maybe_shortcutable_ancestor_->can_shortcut_) {
      return maybe_shortcutable_ancestor_;
    } else {
      ExecedProcess* next = maybe_shortcutable_ancestor_->maybe_shortcutable_ancestor_;
      while (next != nullptr && !next->can_shortcut_)  {
        next = next->maybe_shortcutable_ancestor_;
      }
      maybe_shortcutable_ancestor_ = next;
      return next;
    }
  }
  ExecedProcess* closest_shortcut_point() {
    return can_shortcut() ? this : next_shortcutable_ancestor();
  }
  const char* reason_with_fd(const char* reason, const int fd) const;
  /** Reason for this process can't be short-cut */
  const char* cant_shortcut_reason_ = nullptr;
  /** Reason for this process can't be short-cut */
  const char* shortcut_result_ = nullptr;
  /** Process the event preventing short-cutting happened in */
  const Process *cant_shortcut_proc_ = NULL;
  /** Make jobserver FIFO */
  const FileName* jobserver_fifo_ = nullptr;
  DISALLOW_COPY_AND_ASSIGN(ExecedProcess);
};


}  /* namespace firebuild */
#endif  // FIREBUILD_EXECED_PROCESS_H_
