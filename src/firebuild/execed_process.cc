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
      maybe_shortcutable_ancestor_(parent ? (parent->exec_point()->can_shortcut_
                                             ? parent->exec_point()
                                             : parent->exec_point()->next_shortcutable_ancestor())
                                   : nullptr),
      initial_wd_(initial_wd), wds_(), failed_wds_(), args_(args), env_vars_(env_vars),
      executable_(executable), executed_path_(executed_path), libs_(libs), file_usages_(),
      created_pipes_(), cacher_(NULL) {
  TRACKX(FB_DEBUG_PROC, 0, 1, Process, this,
         "pid=%d, ppid=%d, initial_wd=%s, executable=%s, parent=%s",
         pid, ppid, D(initial_wd), D(executable), D(parent));

  if (parent != NULL) {
    assert(parent->state() == FB_PROC_TERMINATED);
    // add as exec child of parent
    parent->set_exec_pending(false);
    parent->reset_file_fd_pipe_refs();

    // clear a previous exit status, just in case an atexit handler performed the exec
    parent->set_exit_status(-1);
    parent->set_exec_child(this);
  }
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
    parent_exec_point()->register_file_usage(executable(), executable(),
                                             FILE_ACTION_OPEN, O_RDONLY, 0);
    for (const auto& lib : libs_) {
      parent_exec_point()->register_file_usage(lib, lib, FILE_ACTION_OPEN, O_RDONLY, 0);
    }
  }

  /* Find the inherited outbound pipes.
   * Group them according to which fds belongs to the same pipe and which to different.
   * E.g. if fd 1 & 2 point to the same pipe and fd 3 to another one then build up [[1, 2], [3]].
   * The outer list (according to the lowest fd) and the inner lists are all sorted. */
  std::vector<inherited_pipe_t> inherited_pipes;
  /* This iterates over the fds in increasing order. */
  for (auto file_fd : *fds()) {
    std::shared_ptr<Pipe> pipe;
    if (!file_fd || (file_fd->flags() & O_ACCMODE) != O_WRONLY || !(pipe = file_fd->pipe())) {
      continue;
    }
    bool found = false;
    for (inherited_pipe_t& inherited_pipe : inherited_pipes) {
      if (pipe.get() == get_fd(inherited_pipe.fds[0])->pipe().get()) {
        inherited_pipe.fds.push_back(file_fd->fd());
        found = true;
        break;
      }
    }
    if (!found) {
      inherited_pipe_t inherited_pipe;
      inherited_pipe.fds.push_back(file_fd->fd());
      inherited_pipes.push_back(inherited_pipe);
    }
  }
  set_inherited_pipes(inherited_pipes);

  if (FB_DEBUGGING(FB_DEBUG_PROC)) {
    FB_DEBUG(FB_DEBUG_PROC, "Client-side fds of pipes are:");
    for (const inherited_pipe_t& inherited_pipe : inherited_pipes) {
      std::string arr = "  [";
      bool add_sep = false;
      for (int fd : inherited_pipe.fds) {
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

  // store results for this process
  Process::exit_result(status, utime_u, stime_u);
  // propagate to parents exec()-ed this FireBuild process
  propagate_exit_status(status);
}

void ExecedProcess::do_finalize() {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "");

  // store data for shortcutting
  if (cacher_ && !was_shortcut() && can_shortcut() && aggr_cpu_time_u() >= min_cpu_time_u) {
    cacher_->store(this);
  }

  file_usages_.clear();
  fds()->clear();
  inherited_pipes_.clear();
  if (!generate_report) {
    args().clear();
    env_vars().clear();
    libs_.clear();
  }

  /* Propagate resource usage. */
  if (parent_exec_point()) {
    parent_exec_point()->add_children_cpu_time_u(aggr_cpu_time_u());
  }

  // Call the base class's method
  Process::do_finalize();

  for (const auto& pipe : created_pipes_) {
    pipe->finish();
  }
}

/**
 * This is called on file operation methods, for all the ExecedProcess
 * ancestors, excluding the exec_point where the file operation
 * occurred. The method does the necessary administration, and bubbles
 * it upwards to the root.
 *
 * In case of non-shortcutted processes, in the exec_point the method
 * register_file_usage() performed the necessary administration before
 * beginning to bubble up this event.
 *
 * In case of shortcutted processes, it's the shortcutting itself that
 * performs the file operations, no administration is necessary there.
 */
void ExecedProcess::propagate_file_usage(const FileName *name,
                                         const FileUsage* fu_change) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "name=%s, fu_change=%s", D(name), D(fu_change));

  const FileUsage *fu;
  bool propagate = false;
  auto it = file_usages_.find(name);
  if (it != file_usages_.end()) {
    fu = it->second;
    const FileUsage* merged_fu = fu->merge(fu_change);
    if (merged_fu != fu) {
      it.value() = fu = merged_fu;
      propagate = true;
    }
  } else {
    file_usages_[name] = fu = fu_change;
    propagate = true;
  }

  /* Propagage change further if needed. */
  if (propagate) {
    ExecedProcess* next = generate_report ? parent_exec_point() : next_shortcutable_ancestor();
    if (next) {
      next->propagate_file_usage(name, fu);
    }
  }
}

/**
 * This is called on the exec_point of a non-shortcutted process when an
 * open() or similar call is intercepted. Converts the input into a
 * FileUsage, stat'ing the file, computing its checksum if necessary.
 * Registers the file operation, and bubbles it upwards to the root via
 * propagate_file_usage().
 * Looks at the contents of `actual_file`, but registers as if the event
 * happened to `name`.
 */
bool ExecedProcess::register_file_usage(const FileName *name,
                                        const FileName *actual_file,
                                        FileAction action,
                                        int flags,
                                        int error) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "name=%s, actual_file=%s, flags=%d, error=%d",
         D(name), D(actual_file), flags, error);

  if (!can_shortcut_ && !generate_report) {
    /* Register at the first shortcutable ancestor instead. */
    ExecedProcess* next = next_shortcutable_ancestor();
    if (next) {
      return next->register_file_usage(name, actual_file, action, flags, error);
    } else {
      return true;
    }
  }

  const FileUsage *fu = nullptr;
  auto it = file_usages_.find(name);
  if (it != file_usages_.end()) {
    fu = it->second;
  }
  if (fu) {
    /* The process already used this file. The initial state was already
     * recorded. We obtain a new FileUsage object which represents the
     * modifications to apply currently, which is at most the written_
     * flag, and then we propagate this upwards to be applied.
     */
    const FileUsage *fu_change = FileUsage::Get(actual_file, action, flags, error, false);
    if (!fu_change) {
      /* Error */
      return false;
    } else if (fu_change == fu) {
      /* This file usage is already registered identically for this file and is also propoagated */
      return true;
    }
    const FileUsage* merged_fu = fu->merge(fu_change);
    if (merged_fu != fu) {
      it.value() = merged_fu;
      if (parent_exec_point()) {
        parent_exec_point()->propagate_file_usage(name, merged_fu);
      }
    }
  } else {
    /* Checking only here because files at ignore locations would never be added, thus found. */
    if (name->is_at_locations(ignore_locations)) {
      FB_DEBUG(FB_DEBUG_FS, "Ignoring file usage: " + d(name));
      return true;
    }

    /* The process opens this file for the first time. Compute whatever
     * we need to know about its initial state. Use that same object to
     * propagate the changes upwards. */
    fu = FileUsage::Get(actual_file, action, flags, error, true);
    if (!fu) {
      /* Error */
      return false;
    }
    file_usages_[name] = fu;
    if (parent_exec_point()) {
      parent_exec_point()->propagate_file_usage(name, fu);
    }
  }
  return true;
}

/**
 * See the other register_file_usage().
 * This one does not look at the file system, but instead registers the given fu_change.
 */
bool ExecedProcess::register_file_usage(const FileName *name,
                                        const FileUsage* fu_change) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "name=%s, fu_change=%s", D(name), D(fu_change));

  if (name->is_at_locations(ignore_locations)) {
    FB_DEBUG(FB_DEBUG_FS, "Ignoring file usage: " + d(name));
    return true;
  }

  const FileUsage *fu = nullptr;
  auto it = file_usages_.find(name);
  if (it != file_usages_.end()) {
    fu = it->second;
  }
  if (fu) {
    const FileUsage* merged_fu = fu->merge(fu_change);
    if (merged_fu == fu) {
      return true;
    } else {
      it.value() = fu = merged_fu;
    }
  } else {
    file_usages_[name] = fu = fu_change;
  }
  if (parent_exec_point()) {
    parent_exec_point()->propagate_file_usage(name, fu);
  }
  return true;
}

/**
 * Register that the parent (a.k.a. dirname) of the given path does exist and is a directory, and
 * bubbles it upwards to the root. See #259 for rationale.
 * To be called on the exec_point of a non-shortcutted process when something is successfully done
 * to the given file.
 */
bool ExecedProcess::register_parent_directory(const FileName *name) {
  TRACKX(FB_DEBUG_PROC, 1, 1, Process, this, "name=%s", D(name));

  /* name is canonicalized, so just simply strip the last component */
  ssize_t slash_pos = name->length() - 1;
  for (; slash_pos >= 0; slash_pos--) {
    if (name->c_str()[slash_pos] == '/') {
      break;
    }
  }

  if (slash_pos == 0) {
    /* don't bother registering "/" */
    return true;
  } else if (slash_pos == -1) {
    return false;
  }

  char* parent_name = reinterpret_cast<char*>(alloca(slash_pos + 1));
  memcpy(parent_name, name->c_str(), slash_pos);
  parent_name[slash_pos] = '\0';

  return register_file_usage(FileName::Get(parent_name, slash_pos), FileUsage::Get(ISDIR));
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

    for (const inherited_pipe_t& inherited_pipe : inherited_pipes_) {
      if (inherited_pipe.recorder) {
        inherited_pipe.recorder->deactivate();
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

  // sort files before printing
  std::vector<file_file_usage> ordered_file_usages;
  for (auto& pair : file_usages()) {
    ordered_file_usages.push_back({pair.first, pair.second});
  }
  std::sort(ordered_file_usages.begin(), ordered_file_usages.end(), file_file_usage_cmp);

  fprintf(stream, "%s fcreated: [", indent);
  for (auto& ffu : ordered_file_usages) {
    if (ffu.usage->initial_state() != ISREG_WITH_HASH && ffu.usage->written()) {
      fprintf(stream, "\"%s\",", ffu.file->c_str());
    }
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s fmodified: [", indent);
  for (auto& ffu : ordered_file_usages) {
    if (ffu.usage->initial_state() == ISREG_WITH_HASH && ffu.usage->written()) {
      fprintf(stream, "\"%s\",", ffu.file->c_str());
    }
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s fread: [", indent);
  for (auto& ffu : ordered_file_usages) {
    if (ffu.usage->initial_state() == ISREG_WITH_HASH && !ffu.usage->written()) {
      fprintf(stream, "\"%s\",", ffu.file->c_str());
    }
  }
  fprintf(stream, "],\n");

  fprintf(stream, "%s fnotf: [", indent);
  for (auto& ffu : ordered_file_usages) {
    if (ffu.usage->initial_state() != ISREG_WITH_HASH && !ffu.usage->written()) {
      fprintf(stream, "\"%s\",", ffu.file->c_str());
    }
  }
  fprintf(stream, "],\n");

  if (state() != FB_PROC_FINALIZED) {
    // something went wrong
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

  for (auto& pair : file_usages_) {
    delete(pair.second);
  }
  if (cacher_) {
    cacher_->erase_fingerprint(this);
  }
}

}  // namespace firebuild
