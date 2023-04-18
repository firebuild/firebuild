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

#ifndef FIREBUILD_PIPE_RECORDER_H_
#define FIREBUILD_PIPE_RECORDER_H_

#include <sys/uio.h>

#include <memory>
#include <string>
#include <vector>

#include "firebuild/debug.h"
#include "firebuild/blob_cache.h"
#include "firebuild/hash.h"
#include "firebuild/cxx_lang_utils.h"

namespace firebuild {

class ExecedProcess;
class Pipe;

/*
 * Pipe Recording Overview
 *
 * As described in Pipe's documentation, a Pipe instance represents what would have been a single
 * unnamed Unix pipe (or the terminal) if we didn't intervene.
 *
 * It might receive traffic from different actual Unix file descriptors (due to how replacing that
 * pipe with named pipes works). They need to be stored in the cache differently.
 *
 * Example:
 *
 * - Proc0 creates a pipe, writes Text0 into it.
 *
 *   The Pipe objects is created, the pipe's traffic is routed through it. No PipeRecorder yet,
 *   Text0 is forwarded but not recorded. This is because no matter where we will shortcut in a
 *   later run, this data won't be replayed.
 *
 * - Proc0 exec()s or fork()+exec()s Proc1, Proc1 writes Text1.
 *
 *   Pipe is one of Proc1's inherited_files of type FD_PIPE_OUT. A PipeRecorder instance Rec1 is
 *   created when Proc1 is accepted, and is placed at the corresponding inherited_file's recorder.
 *   Furthermore, in the Pipe object, for the pipe_end that corresponds to Proc1's reopened fd, this
 *   recorder is added as the only recorder. When Text1 is seen, Rec1 opens the backing File1 and
 *   stores Text1.
 *
 * - Proc1 exec()s or fork()+exec()s Proc2, Proc2 writes Text2.
 *
 *   Pipe is one of Proc2's inherited_files of type FD_PIPE_OUT. A PipeRecorder instance Rec2 is
 *   created when Proc2 is accepted, and is placed at the corresponding inherited_file's recorder.
 *   Furthermore, in the Pipe object, for the pipe_end that corresponds to Proc2's reopened fd, this
 *   recorder is added too, resulting in two recorders: Rec1 and Rec2. Text2 is recorded by both,
 *   i.e. into File1 and File2.
 *
 * - Proc2 exec()s or fork()+exec()s Proc3, Proc3 writes Text3.
 *
 *   At the corresponding inherited_file of Proc3, Rec3 is the recorder. At the Pipe, corresponding
 *   to Proc3's reopened fd, there are now three recorders Rec1, Rec2 and Rec3. Text3 is recorded by
 *   all, i.e. into File1, File2 and File3.
 *
 *   Now File1 contains Text1+Text2+Text3, File2 contains Text2+Text3, and File3 contains Text3.
 *   That is, each cache entry recorded exactly what was written to the given Pipe, from below the
 *   given exec point in the process tree.
 *
 * This example is also shown in this picture. Solid arrows denote pointer-like relationship in the
 * supervisor's memory. Dashed arrows on the left side show the data that travels through the Unix
 * pipes, from the actual intercepted processes that are represented by those Process boxes. Dashed
 * arrows on the right side show how this data travels further inside the supervisor.
 *
 *                                                         ┌───────── Pipe ──────────┐
 *                                                         │ pipe_ends:              │
 *                                                         │  ┌───────────────────┐  │
 *                                       ┌╌╌ Text0 ╌╌╌╌╌╌╌╌│╌>│ recorders: (none) │  │
 *                                       ┆                 │  ├───────────────────┤  │
 *                                       ┆ ┌╌╌ Text1 ╌╌╌╌╌╌│╌>│ recorders:        │  │
 *                                       ┆ ┆               │  │ - Rec1            │╌╌│╌┐
 *                                       ┆ ┆               │  ├───────────────────┤  │ ┆
 *                                       ┆ ┆ ┌╌╌ Text2 ╌╌╌╌│╌>│ recorders:        │  │ ┆
 *                                       ┆ ┆ ┆             │  │ - Rec1            │╌╌│╌┆╌┐
 *                                       ┆ ┆ ┆             │  │ - Rec2            │╌╌│╌┆╌┆╌╌╌┐
 *                                       ┆ ┆ ┆             │  ├───────────────────┤  │ ┆ ┆   ┆
 *                                       ┆ ┆ ┆ ┌╌╌ Text3 ╌╌│╌>│ recorders:        │  │ ┆ ┆   ┆
 *                                       ┆ ┆ ┆ ┆           │  │ - Rec1            │╌╌│╌┆╌┆╌┐ ┆
 *                                       ┆ ┆ ┆ ┆           │  │ - Rec2            │╌╌│╌┆╌┆╌┆╌┆╌┐
 *                                       ┆ ┆ ┆ ┆           │  │ - Rec3            │╌╌│╌┆╌┆╌┆╌┆╌┆╌┐
 * ┌────────────── Proc0 ─────────────┐  ┆ ┆ ┆ ┆           │  └───────────────────┘  │ ┆ ┆ ┆ ┆ ┆ ┆
 * │ named pipe                       │╌╌┘ ┆ ┆ ┆           └─────────────────────────┘ ┆ ┆ ┆ ┆ ┆ ┆
 * └──────────────────────────────────┘    ┆ ┆ ┆                                       ┆ ┆ ┆ ┆ ┆ ┆
 *                   │                     ┆ ┆ ┆      ┌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌ Text1 ╌╌┘ ┆ ┆ ┆ ┆ ┆
 *                   │                     ┆ ┆ ┆      ┆ ┌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌ Text2 ╌╌┘ ┆ ┆ ┆ ┆
 *                   v                     ┆ ┆ ┆      ┆ ┆ ┌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌ Text3 ╌╌┘ ┆ ┆ ┆
 * ┌────────────── Proc1 ─────────────┐    ┆ ┆ ┆      v v v                                  ┆ ┆ ┆
 * │ named pipe                       │╌╌╌╌┘ ┆ ┆    ┌── Rec1 ──┐    ┌───── File1 ─────┐      ┆ ┆ ┆
 * │ inherited FD_PIPE_OUT's recorder │────────────>│ file     │───>│ Text1Text2Text3 │      ┆ ┆ ┆
 * └──────────────────────────────────┘      ┆ ┆    └──────────┘    └─────────────────┘      ┆ ┆ ┆
 *                   │                       ┆ ┆                                             ┆ ┆ ┆
 *                   │                       ┆ ┆      ┌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌ Text2 ╌╌┘ ┆ ┆
 *                   v                       ┆ ┆      ┆ ┌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌ Text3 ╌╌┘ ┆
 * ┌────────────── Proc2 ─────────────┐      ┆ ┆      v v                                        ┆
 * │ named pipe                       │╌╌╌╌╌╌┘ ┆    ┌── Rec2 ──┐    ┌───── File2 ─────┐          ┆
 * │ inherited FD_PIPE_OUT's recorder │────────────>│ file     │───>│ Text2Text3      │          ┆
 * └──────────────────────────────────┘        ┆    └──────────┘    └─────────────────┘          ┆
 *                   │                         ┆                                                 ┆
 *                   │                         ┆                                                 ┆
 *                   v                         ┆      ┌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌ Text3 ╌╌┘
 * ┌────────────── Proc3 ─────────────┐        ┆      v
 * │ named pipe                       │╌╌╌╌╌╌╌╌┘    ┌── Rec3 ──┐    ┌───── File3 ─────┐
 * │ inherited FD_PIPE_OUT's recorder │────────────>│ file     │───>│ Text3           │
 * └──────────────────────────────────┘             └──────────┘    └─────────────────┘
 *
 * In pipe_end, there's an array of recorders. Each incoming piece of data might need to be
 * registered for multiple processes (or none at all) as something that happened to the given
 * process transitively. E.g. Text2 is printed directly by Proc2, yet Proc1 also printed this
 * indirectly, via the help of a child process.
 *
 * If an execed process inherits a pipe (rather than creating it on its own), exactly one of these
 * recorders is the special one (stored in inherited_file's recorder field) which records the
 * traffic generated by this execed process, transitively. This has to be stored in the cache as the
 * data transitively printed by this process, and replayed if we shortcut the process.
 */

/*
 * The static record_data_*() methods take multiple PipeRecorder objects and operate on them
 * simultaneously, rather than on one PipeRecorder at a time. This was designed to hide from the
 * caller that sometimes the desired action is not the same for all the recorders (e.g. the first
 * one fetches the data from the pipe, the next ones clone it across files). This was also designed
 * in preparation for the following TODO: PipeRecorder objects that happen to see the exact same
 * traffic should share the underlying the file, and then clone on demand.
 */

/**
 * PipeRecorder represents a certain traffic (data stream), a subset of the entire traffic that a
 * Pipe object sees, corresponding to what happens in the subtree under an exec point. (See "Pipe
 * Recording Overview" above for detailed explanation.)
 *
 * This object records the traffic, and eventually stores in the cache with the computed hash.
 *
 * Opening the backing file is delayed until actual traffic is encountered. It is handled as a
 * special case if the recorder sees no traffic at all, this won't be stored in the cache.
 *
 * Currently each PipeRecorder object has its own backing file. This should be improved so
 * that PipeRecorder objects that happen to see the exact same traffic share the same backing
 * file.
 */
class PipeRecorder {
 public:
  explicit PipeRecorder(const ExecedProcess *for_proc);
  ~PipeRecorder() { if (!abandoned_) abandon(); }

  /**
   * If there was traffic, store it in the cache and return is_empty_out=false, key_out=[the_hash].
   * If there was no traffic, set is_empty_out=true, key_out is undefined.
   * Set the recorder to abandoned state.
   * Returns false in case of failure.
   */
  bool store(bool *is_empty_out, Hash *key_out, off_t* stored_bytes);
  /** Close the backing fd, drop the data that was written so far. Set to deactivated state. */
  void deactivate();
  /** Close the backing fd, drop the data that was written so far. Set to abandoned state. */
  void abandon();

  /**
   * Returns whether any of the given recorders is active, i.e. still records data.
   */
  static bool has_active_recorder(const std::vector<std::shared_ptr<PipeRecorder>>& recorders);

  /**
   * Record the given data, from an in-memory buffer, to all the given recorders that are still active.
   *
   * See pipe_recorder.h for the big picture, as well as the design rationale behind this static
   * method taking multiple PipeRecorders at once.
   */
  static void record_data_from_buffer(std::vector<std::shared_ptr<PipeRecorder>> *recorders,
                                      const char *buf, ssize_t len);
  /**
   * Record the given data, from the given Unix pipe, to all the given recorders that are still
   * active.
   *
   * The recorders array must contain at least one active recorder.
   *
   * The Unix pipe must have the given amount of data readily available, as guaranteed by a previous
   * tee(2) call. The data is consumed from the pipe.
   *
   * See pipe_recorder.h for the big picture, as well as the design rationale behind this static
   * method taking multiple PipeRecorders at once.
   */
  static void record_data_from_unix_pipe(std::vector<std::shared_ptr<PipeRecorder>> *recorders,
                                         int fd, ssize_t len);
  /**
   * Record the given data, from the beginning of the given regular file, to all the given recorders
   * that are still active.
   *
   * The current seek offset is irrelevant. len must match the file's size.
   *
   * (This is used when replaying and bubbling up pipe traffic.)
   *
   * See in pipe_recorder.h for the big picture, as well as the design rationale behind this static
   * method taking multiple PipeRecorders at once.
   */
  static void record_data_from_regular_fd(std::vector<std::shared_ptr<PipeRecorder>> *recorders,
                                          int fd, ssize_t len);

  static void set_base_dir(const char *dir);

  /* Member debugging method. Not to be called directly, call the global d(obj_or_ptr) instead.
   * level is the nesting level of objects calling each other's d(), bigger means less info to print.
   * See #431 for design and rationale. */
  std::string d_internal(const int level = 0) const;

 private:
  /**
   * Perform the delayed opening of the backing file.
   * To be called the first time when there's data to record.
   */
  void open_backing_file();
  /**
   * Add non-empty data to this PipeRecorder from a memory buffer, using write().
   *
   * Internal private helper. Callers should call the static record_*() methods instead.
   */
  void add_data_from_buffer(const char *buf, ssize_t len);
  /**
   * Add non-empty data to this PipeRecorder from a pipe, using splice().
   *
   * The Unix pipe must have the given amount of data readily available, as guaranteed by a previous
   * tee(2) call. The data is consumed from the pipe.
   *
   * Internal private helper. Callers should call the static record_*() methods instead.
   */
  void add_data_from_unix_pipe(int pipe_fd, ssize_t len);
  /**
   * Add non-empty data to this PipeRecorder, by copying it from another file using copy_file_range().
   *
   * The current seek offset in fd_in is irrelevant.
   *
   * Internal private helper. Callers should call the static record_*() methods instead.
   */
  void add_data_from_regular_fd(int fd_in, loff_t off_in, ssize_t len);

  /* The ExecedProcess we're recording for, i.e. the ExecedProcess that created this PipeRecorder to
   * add to its inherited_files array. Data written to the Pipe by this process or a descendant will
   * be recorded by this PipeRecorder, data written to the Pipe by an ancestor of this process
   * won't. Used for debugging only. */
  const ExecedProcess *for_proc_;
  /* The name of the backing file, if currently opened. */
  char *filename_ = NULL;
  /* The fd, -1 if not yet opened or already closed. */
  int fd_ {-1};
  /* The amount of data written so far. */
  loff_t offset_ {0};

  /* A deactivated PipeRecorder has thrown away all the data it has ever received, and won't record
   * any new data. However, it might still receive data "to record", which will silently be dropped.
   * This is useful when a process can no longer be shortcut, so there's no point in recording the
   * data as seen from its point of view, however, there's still pipe traffic going on. */
  bool deactivated_ {false};
  /* An abandoned PipeRecorder has either stored or thrown away the data it received so far. It
   * asserts that it doesn't receive any new data to record. */
  bool abandoned_ {false};

  /* Unique PipeRecorder ID, for debugging. */
  int id_;

  static int id_counter_;
  /* The location to work with, including the "tmp" subdir. */
  static char *base_dir_;

  DISALLOW_COPY_AND_ASSIGN(PipeRecorder);
};

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const PipeRecorder& recorder, const int level = 0);
std::string d(const PipeRecorder *recorder, const int level = 0);

}  /* namespace firebuild */
#endif  // FIREBUILD_PIPE_RECORDER_H_
