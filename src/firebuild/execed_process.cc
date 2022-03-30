/* Copyright (c) 2014 Balint Reczey <balint@balintreczey.hu> */
/* This file is an unpublished work. All rights reserved. */


#include "firebuild/execed_process.h"

#include <algorithm>
#include <map>
#include <memory>
#include <sstream>

#include <libconfig.h++>

#include "firebuild/config.h"
#include "firebuild/execed_process_cacher.h"
#include "firebuild/forked_process.h"
#include "firebuild/platform.h"
#include "firebuild/utils.h"

extern bool generate_report;

namespace firebuild {

/**
 * Escape std::string for JavaScript
 * from http://stackoverflow.com/questions/7724448/simple-json-string-escape-for-c
 * TODO: use JSONCpp instead to handle all cases
 */
static std::string escapeJsonString(const std::string& input) {
  std::ostringstream ss;
  for (auto iter = input.cbegin(); iter != input.cend(); iter++) {
    switch (*iter) {
      case '\\': ss << "\\\\"; break;
      case '"': ss << "\\\""; break;
      case '/': ss << "\\/"; break;
      case '\b': ss << "\\b"; break;
      case '\f': ss << "\\f"; break;
      case '\n': ss << "\\n"; break;
      case '\r': ss << "\\r"; break;
      case '\t': ss << "\\t"; break;
      default: ss << *iter; break;
    }
  }
  return ss.str();
}

ExecedProcess::ExecedProcess(const int pid, const int ppid,
                             const FileName *initial_wd,
                             const FileName *executable,
                             const FileName *executed_path,
                             const std::vector<std::string>& args,
                             const std::vector<std::string>& env_vars,
                             const std::vector<const FileName*>& libs,
                             Process * parent,
                             std::vector<std::shared_ptr<FileFD>>* fds)
    : Process(pid, ppid, parent ? parent->exec_count() + 1 : 1, initial_wd, parent, fds),
      can_shortcut_(true), was_shortcut_(false),
      maybe_shortcutable_ancestor_(
          (parent && parent->exec_point()) ? parent->exec_point()->closest_shortcut_point()
          : nullptr),
      initial_wd_(initial_wd), wds_(), failed_wds_(), args_(args), env_vars_(env_vars),
      executable_(executable), executed_path_(executed_path), libs_(libs), file_usages_(),
      created_pipes_(), cacher_(NULL) {
  TRACKX(FB_DEBUG_PROC, 0, 1, Process, this,
         "pid=%d, ppid=%d, initial_wd=%s, executable=%s, parent=%s",
         pid, ppid, D(initial_wd), D(executable), D(parent));

  if (parent) {
    assert(parent->state() == FB_PROC_TERMINATED);
    /* add as exec child of parent */
    fork_point_ = parent->fork_point();
    parent->set_exec_pending(false);
    parent->reset_file_fd_pipe_refs();

    /* clear a previous exit status, just in case an atexit handler performed the exec */
    parent->set_exit_status(-1);
    parent->set_exec_child(this);
  }
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

/**
 * Initialization stuff that can only be done after placing the
 * ExecedProcess in the ProcessTree.
 */
void ExecedProcess::initialize() {
  TRACKX(FB_DEBUG_PROC, 0, 1, Process, this, "");

  /* Propagate the opening of the executable and libraries upwards as
   * regular file open events. */
  if (parent_exec_point()) {
    FileUsageUpdate exe_update = FileUsageUpdate::get_from_open_params(executable(), O_RDONLY, 0);
    parent_exec_point()->register_file_usage_update(executable(), exe_update);
    for (const auto& lib : libs_) {
      FileUsageUpdate lib_update = FileUsageUpdate::get_from_open_params(lib, O_RDONLY, 0);
      parent_exec_point()->register_file_usage_update(lib, lib_update);
    }
  }

  /* Find the inherited outgoing pipes.
   * Group them according to which fds belongs to the same pipe and which to different.
   * E.g. if fd 1 & 2 point to the same pipe and fd 3 to another one then build up [[1, 2], [3]].
   * The outer list (according to the lowest fd) and the inner lists are all sorted. */
  std::vector<inherited_outgoing_pipe_t> inherited_outgoing_pipes;
  /* This iterates over the fds in increasing order. */
  for (auto file_fd : *fds()) {
    std::shared_ptr<Pipe> pipe;
    if (!file_fd || (file_fd->flags() & O_ACCMODE) != O_WRONLY || !(pipe = file_fd->pipe())) {
      continue;
    }
    bool found = false;
    for (inherited_outgoing_pipe_t& inherited_outgoing_pipe : inherited_outgoing_pipes) {
      if (pipe.get() == get_fd(inherited_outgoing_pipe.fds[0])->pipe().get()) {
        inherited_outgoing_pipe.fds.push_back(file_fd->fd());
        found = true;
        break;
      }
    }
    if (!found) {
      inherited_outgoing_pipe_t inherited_outgoing_pipe;
      inherited_outgoing_pipe.fds.push_back(file_fd->fd());
      inherited_outgoing_pipes.push_back(inherited_outgoing_pipe);
    }
  }
  set_inherited_outgoing_pipes(inherited_outgoing_pipes);

  if (FB_DEBUGGING(FB_DEBUG_PROC)) {
    FB_DEBUG(FB_DEBUG_PROC, "Client-side fds of pipes are:");
    for (const inherited_outgoing_pipe_t& inherited_outgoing_pipe : inherited_outgoing_pipes) {
      std::string arr = "  [";
      bool add_sep = false;
      for (int fd : inherited_outgoing_pipe.fds) {
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

void ExecedProcess::propagate_exit_status(const int status) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "status=%d", status);

  if (parent()) {
    parent()->set_exit_status(status);
    parent()->propagate_exit_status(status);
  }
}

void ExecedProcess::exit_result(const int status, const int64_t utime_u,
                                const int64_t stime_u) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "status=%d, utime_u=%ld, stime_u=%ld",
         status, utime_u, stime_u);

  /* store results for this process */
  Process::exit_result(status, utime_u, stime_u);
  /* propagate to parents exec()-ed this Firebuild process */
  propagate_exit_status(status);
}

void ExecedProcess::do_finalize() {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "");

  /* store data for shortcutting */
  if (cacher_ && !was_shortcut() && can_shortcut() && aggr_cpu_time_u() >= min_cpu_time_u) {
    cacher_->store(this);
  }

  file_usages_.clear();
  fds()->clear();
  inherited_outgoing_pipes_.clear();
  if (!generate_report) {
    args().clear();
    env_vars().clear();
    libs_.clear();
  }

  /* Propagate resource usage. */
  if (parent_exec_point()) {
    parent_exec_point()->add_children_cpu_time_u(aggr_cpu_time_u());
  }

  /* Call the base class's method */
  Process::do_finalize();

  for (const auto& pipe : created_pipes_) {
    pipe->finish();
  }
}

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

  ExecedProcess *proc = this;
  if (!proc->can_shortcut_ && !generate_report) {
    /* Register at the first shortcutable ancestor instead. */
    proc = proc->next_shortcutable_ancestor();
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
      }
      /* Note: This can update "update" if some value is computed now and cached there. In that
       * case, in the next iteration of the loop "update" will already contain this value. */
      fu_new = fu->merge(update);
      if (!fu_new) {
        disable_shortcutting_bubble_up("Could not register unsupported file usage combination");
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

    if (!generate_report) {
      proc = proc->next_shortcutable_ancestor();
    } else {
      proc = proc->exec_point();
    }
  }
  return true;
}

/**
 * Register that the parent (a.k.a. dirname) of the given path does (or does not) exist and is of
 * the given "type" (e.g. ISDIR, NOTEXIST), and bubbles it up to the root.
 */
bool ExecedProcess::register_parent_directory(const FileName *name,
                                              FileType type) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "name=%s", D(name));

  const FileName* const parent_dir = FileName::GetParentDir(name->c_str(), name->length());

  if (!parent_dir) {
    return false;
  } else if (parent_dir->length() == 1 && parent_dir->c_str()[0] == '/') {
    /* don't bother registering "/" */
    return true;
  }

  FileUsageUpdate update(parent_dir, type);
  assert(update.parent_type() == DONTKNOW);
  return register_file_usage_update(parent_dir, update);
}

/* Find and apply shortcut */
bool ExecedProcess::shortcut() {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "");

  if (can_shortcut() && cacher_) {
    return cacher_->shortcut(this);
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
  disable_shortcutting_bubble_up_to_excl(stop, reason, p, shortcutable_ancestor,
                                         shortcutable_ancestor_is_set);
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
  disable_shortcutting_bubble_up(reason, p);
  FB_DEBUG(FB_DEBUG_PROC, "fd: " + d(fd));
}

void ExecedProcess::disable_shortcutting_bubble_up(const char* reason,
                                                   const FileName& file,
                                                   const ExecedProcess *p) {
  disable_shortcutting_bubble_up(reason, p);
  FB_DEBUG(FB_DEBUG_PROC, "file: " + d(file));
}

void ExecedProcess::disable_shortcutting_bubble_up(const char* reason,
                                                   const std::string& str,
                                                   const ExecedProcess *p) {
  disable_shortcutting_bubble_up(reason, p);
  FB_DEBUG(FB_DEBUG_PROC, d(str));
}

void ExecedProcess::disable_shortcutting_only_this(const char* reason,
                                                   const ExecedProcess *p) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "reason=%s, source=%s", D(reason), D(p));

  if (can_shortcut_) {
    can_shortcut_ = false;
    assert(cant_shortcut_reason_ == nullptr);
    cant_shortcut_reason_ = reason;
    assert_null(cant_shortcut_proc_);
    cant_shortcut_proc_ = p ? p : this;
    FB_DEBUG(FB_DEBUG_PROC, "Command " + d(executable_->c_str())
             + " can't be short-cut due to: " + reason + ", " + d(this));

    for (const inherited_outgoing_pipe_t& inherited_outgoing_pipe : inherited_outgoing_pipes_) {
      if (inherited_outgoing_pipe.recorder) {
        inherited_outgoing_pipe.recorder->deactivate();
      }
    }
  }
}

void ExecedProcess::export2js_recurse(const unsigned int level, FILE* stream,
                                      unsigned int *nodeid) {
  if (level > 0) {
    fprintf(stream, "\n");
  }
  fprintf(stream, "%s{", std::string(2 * level, ' ').c_str());

  export2js(level, stream, nodeid);
  fprintf(stream, "%s children: [", std::string(2 * level, ' ').c_str());
  Process::export2js_recurse(level, stream, nodeid);
  if (level == 0) {
    fprintf(stream, "]};\n");
  } else {
    fprintf(stream, "]},\n");
  }
}


void ExecedProcess::export2js(const unsigned int level,
                              FILE* stream, unsigned int * nodeid) {
  // TODO(rbalint): escape all strings properly
  auto indent_str = std::string(2 * level, ' ');
  const char* indent = indent_str.c_str();

  fprintf(stream, "name:\"%s\",\n", args()[0].c_str());
  fprintf(stream, "%s id: %u,\n", indent, (*nodeid)++);
  fprintf(stream, "%s pid: %u,\n", indent, pid());
  fprintf(stream, "%s ppid: %u,\n", indent, ppid());
  fprintf(stream, "%s fb_pid: %u,\n", indent, fb_pid());
  fprintf(stream, "%s initial_wd:\"%s\",\n", indent, initial_wd()->c_str());
  fprintf(stream, "%s exe:\"%s\",\n", indent, executable()->c_str());
  fprintf(stream, "%s state: %u,\n", indent, state());
  if (was_shortcut()) {
    fprintf(stream, "%s was_shortcut: true,\n", indent);
  }
  if (!can_shortcut_) {
    fprintf(stream, "%s cant_sc_reason: \"%s\",\n",
            indent, escapeJsonString(cant_shortcut_reason_).c_str());
    if (cant_shortcut_proc_->exec_proc()->fb_pid() != fb_pid()) {
      fprintf(stream, "%s cant_sc_fb_pid: \"%u\",\n",
              indent, cant_shortcut_proc_->exec_proc()->fb_pid());
    }
  }
  fprintf(stream, "%s args: [", indent);
  for (auto& arg : args()) {
    fprintf(stream, "\"%s\",", escapeJsonString(arg).c_str());
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s env: [", indent);
  for (auto& env : env_vars()) {
    fprintf(stream, "\"%s\",", escapeJsonString(env).c_str());
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s libs: [", indent);
  for (auto& lib : libs()) {
    fprintf(stream, "\"%s\",", lib->c_str());
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s wds: [", indent);
  for (auto& wd : wds()) {
    fprintf(stream, "\"%s\",", wd->c_str());
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s failed_wds: [", indent);
  for (auto& f_wd : failed_wds()) {
    fprintf(stream, "\"%s\",", f_wd->c_str());
  }
  fprintf(stream, "],\n");

  /* sort files before printing */
  std::vector<file_file_usage> ordered_file_usages;
  for (auto& pair : file_usages()) {
    ordered_file_usages.push_back({pair.first, pair.second});
  }
  std::sort(ordered_file_usages.begin(), ordered_file_usages.end(), file_file_usage_cmp);

  fprintf(stream, "%s fcreated: [", indent);
  for (auto& ffu : ordered_file_usages) {
    bool isreg_with_hash = ffu.usage->initial_type() == ISREG && ffu.usage->initial_hash_known();
    if (!isreg_with_hash && ffu.usage->written()) {
      fprintf(stream, "\"%s\",", ffu.file->c_str());
    }
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s fmodified: [", indent);
  for (auto& ffu : ordered_file_usages) {
    bool isreg_with_hash = ffu.usage->initial_type() == ISREG && ffu.usage->initial_hash_known();
    if (isreg_with_hash && ffu.usage->written()) {
      fprintf(stream, "\"%s\",", ffu.file->c_str());
    }
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s fread: [", indent);
  for (auto& ffu : ordered_file_usages) {
    bool isreg_with_hash = ffu.usage->initial_type() == ISREG && ffu.usage->initial_hash_known();
    if (isreg_with_hash && !ffu.usage->written()) {
      fprintf(stream, "\"%s\",", ffu.file->c_str());
    }
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s fnotf: [", indent);
  for (auto& ffu : ordered_file_usages) {
    bool isreg_with_hash = ffu.usage->initial_type() == ISREG && ffu.usage->initial_hash_known();
    if (!isreg_with_hash && !ffu.usage->written()) {
      fprintf(stream, "\"%s\",", ffu.file->c_str());
    }
  }
  fprintf(stream, "],\n");

  if (state() != FB_PROC_FINALIZED) {
    // TODO(rbalint) something went wrong
  }
  if (exit_status() != -1) {
    fprintf(stream, "%s exit_status: %u,\n", indent, exit_status());
  }
  fprintf(stream, "%s utime_u: %lu,\n", indent, utime_u());
  fprintf(stream, "%s stime_u: %lu,\n", indent, stime_u());
  fprintf(stream, "%s aggr_time: %lu,\n", indent, aggr_cpu_time_u());
}

/* For debugging, a short imprecise reminder of the command line. Omits the path to the
 * executable, and strips off the middle. Does not escape or quote. */
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

/* Member debugging method. Not to be called directly, call the global d(obj_or_ptr) instead.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
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

  if (cacher_) {
    cacher_->erase_fingerprint(this);
  }
}

}  /* namespace firebuild */
