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


#ifndef FIREBUILD_FORKED_PROCESS_H_
#define FIREBUILD_FORKED_PROCESS_H_

#include <cassert>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "firebuild/process.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild {

class ExecedProcess;

class ForkedProcess : public Process {
 public:
  explicit ForkedProcess(const int pid, const int ppid, Process* parent,
                         std::vector<std::shared_ptr<FileFD>>* fds);
  virtual ~ForkedProcess();
  ExecedProcess* exec_point() {return exec_point_;}
  ForkedProcess* fork_point() {return this;}
  const ForkedProcess* fork_point() const {return this;}
  const ExecedProcess* exec_point() const {return exec_point_;}
  int exit_status() const {return exit_status_;}
  void set_exit_status(const int e) {exit_status_ = e;}
  bool has_orphan_descendant() const {return has_orphan_descendant_;}
  void set_has_orphan_descendant_bubble_up();
  /**
   * Fail to change to a working directory
   */
  void handle_fail_wd(const char * const d) {
    assert(parent());
    parent()->handle_fail_wd(d);
  }
  /**
   * Record visited working directory
   */
  void add_wd(const FileName *d) {
    assert(parent());
    parent()->add_wd(d);
  }
  Process* exec_proc() const {return parent()->exec_proc();}

  void do_finalize();
  void set_on_finalized_ack(int id, int fd) {
    assert(on_finalized_ack_id_ == -1 && on_finalized_ack_fd_ == -1);
    on_finalized_ack_id_ = id;
    on_finalized_ack_fd_ = fd;
  }
  bool has_on_finalized_ack_set() const {
    return on_finalized_ack_id_ != -1;
  }
  void maybe_send_on_finalized_ack();
  /* Parent's wait for this process can be ACK-ed. */
  bool can_ack_parent_wait() const {
    return (state() == FB_PROC_FINALIZED)
        || (state() == FB_PROC_TERMINATED && has_orphan_descendant()
            && !any_child_not_finalized_or_terminated_with_orphan());
  }
  bool been_waited_for() const {return been_waited_for_;}
  void set_been_waited_for();
  bool orphan() const {return orphan_;}
  void set_orphan() {orphan_ = true;}

  /* Member debugging method. Not to be called directly, call the global d(obj_or_ptr) instead.
   * level is the nesting level of objects calling each other's d(), bigger means less info to print.
   * See #431 for design and rationale. */
  virtual std::string d_internal(const int level = 0) const;

 private:
  /**
   * Exit status of the process.
   * 0..255 if the process exited cleanly and the parent successfully waited for it.
   * -1, if the process did not exit cleanly (died due to a signal) or the parent has not
   * waited for it yet.
   */
  int exit_status_ = -1;
  ExecedProcess *exec_point_ {};
  int on_finalized_ack_id_ = -1;
  bool been_waited_for_  = false;
  bool orphan_  = false;
  bool has_orphan_descendant_ = false;
  int on_finalized_ack_fd_ = -1;
  virtual void disable_shortcutting_only_this(const std::string &reason, const Process *p = NULL) {
    (void) reason;  /* unused */
    (void) p;       /* unused */
  }
  DISALLOW_COPY_AND_ASSIGN(ForkedProcess);
};


}  /* namespace firebuild */
#endif  // FIREBUILD_FORKED_PROCESS_H_
