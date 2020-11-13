/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include "firebuild/execed_process_cacher.h"

#include <unistd.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "firebuild/debug.h"
#include "firebuild/execed_process.h"
#include "firebuild/fb-cache.pb.h"
#include "firebuild/hash_cache.h"

namespace firebuild {

/**
 * A protobuf FieldValuePrinter that adds the hex hash to fields of
 * type "bytes" that happen to be exactly as long as our hashes, for
 * easier debugging.
 *
 * Similarly, int32s are also printed in octal (useful at file permissions).
 *
 * Other types are printed as usual.
 *
 * False positives might happen at e.g. short filenames, that's okay.
 *
 * We could probably go for a solution that's aware of the exact meaning
 * of fields and really only adds the hex string for hashes. It would go
 * something like
 *     Printer::RegisterFieldValuePrinter(
 *         msg.GetDescriptor()->FindFieldByName("hash"), ...)
 * but then the exact message type we store would be hardwired to
 * MultiCache.
 */
class ProtobufHashHexValuePrinter : public google::protobuf::TextFormat::FieldValuePrinter {
 public:
  std::string PrintBytes(const std::string& val) const override {
    /* Call the base class to print as usual. */
    std::string ret = google::protobuf::TextFormat::FieldValuePrinter::PrintBytes(val);
    /* Append the hex value if desirable. */
    if (val.size() == Hash::hash_size()) {
      ret += "  # ";
      char buf[3];
      for (unsigned int i = 0; i < Hash::hash_size(); i++) {
        snprintf(buf, sizeof(buf), "%02x", (unsigned char)(val[i]));
        ret += buf;
      }
    }
    return ret;
  }
  std::string PrintInt32(google::protobuf::int32 val) const override {
    /* Call the base class to print as usual. */
    std::string ret = google::protobuf::TextFormat::FieldValuePrinter::PrintInt32(val);
    /* Append the octal value if desirable. */
    if (val > 0 && val <= 07777) {
      ret += "  # 0";
      if (val >= 01000) {
        ret += ('0' + val / 01000);
      }
      ret += ('0' + val % 01000 / 0100);
      ret += ('0' + val %  0100 /  010);
      ret += ('0' + val %   010);
    }
    return ret;
  }
};

/**
 * One object is responsible for handling the fingerprinting and caching
 * of multiple ExecedProcesses which potentially come from / go to the
 * same cache.
 */
ExecedProcessCacher::ExecedProcessCacher(Cache *cache,
                                         MultiCache *multi_cache,
                                         bool no_store,
                                         bool no_fetch,
                                         const libconfig::Setting& envs_skip) :
    cache_(cache), multi_cache_(multi_cache), no_store_(no_store), no_fetch_(no_fetch),
    envs_skip_(envs_skip), fingerprints_(), fingerprint_msgs_() { }

/**
 * Helper for fingerprint() to decide which env vars matter
 */
bool ExecedProcessCacher::env_fingerprintable(const std::string& name_and_value) const {
  /* Strip off the "=value" part. */
  std::string name = name_and_value.substr(0, name_and_value.find('='));

  /* Env vars to skip, taken from the config files.
   * Note: FB_SOCKET is already filtered out in the interceptor. */
  for (int i = 0; i < envs_skip_.getLength(); i++) {
    std::string item = envs_skip_[i];
    if (name == item) {
      return false;
    }
  }
  return true;
}

/**
 * Compute the fingerprint, store it keyed by the process in fingerprints_.
 * Also store fingerprint_msgs_ if debugging is enabled.
 */
bool ExecedProcessCacher::fingerprint(const ExecedProcess *proc) {
  auto fp_msg = new msg::ProcessFingerprint();

  fp_msg->set_cwd(proc->cwd());
  for (auto& arg : proc->args()) {
    fp_msg->add_arg(arg);
  }

  /* Already sorted by the interceptor */
  for (auto& env : proc->env_vars()) {
    if (env_fingerprintable(env)) {
      fp_msg->add_env(env);
    }
  }

  /* The executable and its hash */
  firebuild::Hash hash;
  if (!hash_cache->get_hash(proc->executable(), &hash)) {
    delete fp_msg;
    return false;
  }
  fp_msg->mutable_executable()->set_path(proc->executable());
  fp_msg->mutable_executable()->set_hash(hash.to_binary());

  for (auto& lib : proc->libs()) {
    if (lib == "linux-vdso.so.1") {
      continue;
    }
    if (!hash_cache->get_hash(lib, &hash)) {
      delete fp_msg;
      return false;
    }
    auto entry = fp_msg->add_libs();
    entry->set_path(lib);
    entry->set_hash(hash.to_binary());
  }

  hash.set_from_protobuf(*fp_msg);

  fingerprints_[proc] = hash;
  if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
    fingerprint_msgs_[proc] = fp_msg;
  } else {
    delete fp_msg;
  }
  return true;
}

void ExecedProcessCacher::erase_fingerprint(const ExecedProcess *proc) {
  fingerprints_.erase(proc);
  if (FB_DEBUGGING(FB_DEBUG_CACHE) && fingerprint_msgs_.count(proc) > 0) {
    delete fingerprint_msgs_[proc];
    fingerprint_msgs_.erase(proc);
  }
}

struct file_file_usage {
  const std::string* file;
  const FileUsage* file_usage;
};

bool file_file_usage_cmp(const file_file_usage& lhs, const file_file_usage& rhs) {
  return *lhs.file < *rhs.file;
}

void ExecedProcessCacher::store(const ExecedProcess *proc) {
  if (no_store_) {
    return;
  }

  Hash fingerprint = fingerprints_[proc];

  /* Go through the files the process opened for reading and/or writing.
   * Construct the protobuf describing the initial and the final state
   * of them. */
  firebuild::msg::ProcessInputsOutputs pio;

  std::vector<file_file_usage> sorted_file_usages;
  for (const auto& pair : proc->file_usages()) {
    sorted_file_usages.push_back({&pair.first, pair.second});
  }
  std::sort(sorted_file_usages.begin(), sorted_file_usages.end(), file_file_usage_cmp);

  for (const auto& ffu : sorted_file_usages) {
    const std::string& filename = *ffu.file;
    const FileUsage* fu = ffu.file_usage;

    /* If the file's initial contents matter, record it in pb's "inputs".
     * This is purely data conversion from one format to another. */
    switch (fu->initial_state()) {
      case DONTCARE:
        /* Nothing to do. */
        break;
      case ISREG_WITH_HASH: {
        firebuild::msg::File* input = pio.mutable_inputs()->add_path_isreg_with_hash();
        input->set_path(filename);
        input->set_hash(fu->initial_hash().to_binary());
        break;
      }
      case ISREG:
        pio.mutable_inputs()->add_path_isreg(filename);
        break;
      case ISDIR_WITH_HASH: {
        firebuild::msg::File* input = pio.mutable_inputs()->add_path_isdir_with_hash();
        input->set_path(filename);
        input->set_hash(fu->initial_hash().to_binary());
        break;
      }
      case ISDIR:
        pio.mutable_inputs()->add_path_isdir(filename);
        break;
      case NOTEXIST_OR_ISREG_EMPTY:
        pio.mutable_inputs()->add_path_notexist_or_isreg_empty(filename);
        break;
      case NOTEXIST:
        pio.mutable_inputs()->add_path_notexist(filename);
        break;
      default:
        assert(false);
    }

    /* If the file's final contents matter, place it in the file cache,
     * and also record it in pb's "outputs". This actually needs to
     * compute the checksums now. */
    if (fu->written()) {
      Hash hash;
      struct stat st;
      if (stat(filename.c_str(), &st) == 0) {
        if (S_ISREG(st.st_mode)) {
          /* TODO don't store and don't record if it was read with the same hash. */
          if (!cache_->store_file(filename, &hash)) {
            /* unexpected error, now what? */
            FB_DEBUG(FB_DEBUG_CACHING, "Could not store blob in cache, not writing shortcut info");
            return;
          }
          firebuild::msg::File* file_written = pio.mutable_outputs()->add_path_isreg_with_hash();
          file_written->set_path(filename);
          file_written->set_hash(hash.to_binary());
          // TODO(egmont) fail if setuid/setgid/sticky is set
          file_written->set_mode(st.st_mode & 07777);
        } else if (S_ISDIR(st.st_mode)) {
          firebuild::msg::File* file_written = pio.mutable_outputs()->add_path_isdir();
          file_written->set_path(filename);
          // TODO(egmont) fail if setuid/setgid/sticky is set
          file_written->set_mode(st.st_mode & 07777);
        } else {
          // TODO(egmont) handle other types of entries
        }
      } else {
        if (fu->initial_state() != NOTEXIST) {
          pio.mutable_outputs()->add_path_notexist(filename);
        }
      }
    }
  }

  pio.mutable_outputs()->set_exit_status(proc->exit_status());
  // TODO(egmont) Add all sorts of other stuff

  std::string debug_header;
  msg::ProcessFingerprint *debug_msg = NULL;
  google::protobuf::TextFormat::Printer *printer = NULL;
  if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
    debug_header = pretty_print_timestamp() + "\n\n";

    debug_msg = fingerprint_msgs_[proc];

    const auto pb_hash_hex_value_printer = new ProtobufHashHexValuePrinter();
    printer = new google::protobuf::TextFormat::Printer();
    printer->SetDefaultFieldValuePrinter(pb_hash_hex_value_printer);  /* takes ownership */
  }

  /* Store in the cache everything about this process. */
  multi_cache_->store_protobuf(fingerprint, pio, debug_msg, debug_header, printer, NULL);
  delete printer;
}

/**
 * Check whether the given ProcessInputs matches the file system's
 * current contents.
 */
static bool pi_matches_fs(const msg::ProcessInputs& pi, const Hash& fingerprint) {
  struct stat64 st;
  for (const msg::File& file : pi.path_isreg_with_hash()) {
    Hash on_fs_hash, in_cache_hash;
    bool is_dir;
    if (!hash_cache->get_hash(file.path(), &on_fs_hash, &is_dir) || is_dir) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   " + fingerprint.to_hex()
               + " mismatches e.g. at " + pretty_print_string(file.path())
               + ": regular file expected but does not exist or something else found");
      return false;
    }
    in_cache_hash.set_hash_from_binary(file.hash());
    if (on_fs_hash != in_cache_hash) {
      FB_DEBUG(FB_DEBUG_SHORTCUT, "│   " + fingerprint.to_hex() + " mismatches e.g. at " +
                                  pretty_print_string(file.path()) + ": hash differs");
      return false;
    }
  }
  for (const msg::File& file : pi.path_isdir_with_hash()) {
    Hash on_fs_hash, in_cache_hash;
    bool is_dir;
    if (!hash_cache->get_hash(file.path(), &on_fs_hash, &is_dir) || !is_dir) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   " + fingerprint.to_hex()
               + " mismatches e.g. at " + pretty_print_string(file.path())
               + ": directory expected but does not exist or something else found");
      return false;
    }
    in_cache_hash.set_hash_from_binary(file.hash());
    if (on_fs_hash != in_cache_hash) {
      FB_DEBUG(FB_DEBUG_SHORTCUT, "│   " + fingerprint.to_hex() + " mismatches e.g. at " +
                                  pretty_print_string(file.path()) + ": hash differs");
      return false;
    }
  }
  for (const std::string& filename : pi.path_isreg()) {
    if (stat64(filename.c_str(), &st) == -1 || !S_ISREG(st.st_mode)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   " + fingerprint.to_hex()
               + " mismatches e.g. at " + pretty_print_string(filename)
               + ": regular file expected but does not exist or something else found");
      return false;
    }
  }
  for (const std::string& filename : pi.path_isdir()) {
    if (stat64(filename.c_str(), &st) == -1 || !S_ISDIR(st.st_mode)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   " + fingerprint.to_hex()
               + " mismatches e.g. at " + pretty_print_string(filename)
               + ": directory expected but does not exist or something else found");
      return false;
    }
  }
  for (const std::string& filename : pi.path_notexist_or_isreg_empty()) {
    if (stat64(filename.c_str(), &st) != -1 && (!S_ISREG(st.st_mode) || st.st_size > 0)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   " + fingerprint.to_hex()
               + " mismatches e.g. at " + pretty_print_string(filename)
               + ": file expected to be missing or empty, non-empty file or something else found");
      return false;
    }
  }
  for (const std::string& filename : pi.path_notexist()) {
    if (stat64(filename.c_str(), &st) != -1) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   " + fingerprint.to_hex()
               + " mismatches e.g. at " + pretty_print_string(filename)
               + ": path expected to be missing, existing object is found");
      return false;
    }
  }
  return true;
}

/**
 * Look up the cache for an entry describing what this process did the
 * last time.
 *
 * This means fetching all the entries corresponding to the process's
 * fingerprint, and finding the one matching the file system.
 *
 * Returns a new object, to be deleted by the caller, if exactly one
 * match was found.
 */
msg::ProcessInputsOutputs *ExecedProcessCacher::find_shortcut(const ExecedProcess *proc) {
  msg::ProcessInputsOutputs inouts;
  msg::ProcessInputsOutputs *ret = NULL;
  int count = 0;
  Hash fingerprint = fingerprints_[proc];  // FIXME error handling

  FB_DEBUG(FB_DEBUG_SHORTCUT, "│ Candidates:");
  std::vector<Hash> subkeys = multi_cache_->list_subkeys(fingerprint);
  if (subkeys.empty()) {
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   None found");
  }
  for (const Hash& subkey : subkeys) {
    if (!multi_cache_->retrieve_protobuf(fingerprint, subkey, &inouts)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   Cannot retrieve " + subkey.to_hex() + " from multicache, ignoring");
      continue;
    }
    if (!inouts.has_inputs() || pi_matches_fs(inouts.inputs(), subkey)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT, "│   " + subkey.to_hex() + " matches the file system");
      count++;
      if (count == 1) {
        ret = new msg::ProcessInputsOutputs();
        *ret = inouts;
        /* Let's play safe for now and not break out of this loop, let's
         * make sure that there are no other matches. */
      }
      if (count == 2) {
        FB_DEBUG(FB_DEBUG_SHORTCUT,
                 "│   More than 1 matching candidates found, ignoring them all");
        delete ret;
        return NULL;
      }
    }
  }
  return ret;
}

/**
 * Applies the given shortcut.
 *
 * Modifies the file system to match the given instructions. Propagates
 * upwards all the shortcutted file read and write events.
 */
bool ExecedProcessCacher::apply_shortcut(ExecedProcess *proc,
                                         const msg::ProcessInputsOutputs& inouts) {
  if (proc->parent_exec_point()) {
    for (const msg::File& file : inouts.inputs().path_isreg_with_hash()) {
      Hash hash;
      hash.set_hash_from_binary(file.hash());
      FileUsage fu(ISREG_WITH_HASH, hash);
      proc->parent_exec_point()->propagate_file_usage(file.path(), fu);
    }
    for (const msg::File& file : inouts.inputs().path_isdir_with_hash()) {
      Hash hash;
      hash.set_hash_from_binary(file.hash());
      FileUsage fu(ISDIR_WITH_HASH, hash);
      proc->parent_exec_point()->propagate_file_usage(file.path(), fu);
    }
    for (const std::string& filename : inouts.inputs().path_isreg()) {
      FileUsage fu(ISREG);
      proc->parent_exec_point()->propagate_file_usage(filename, fu);
    }
    for (const std::string& filename : inouts.inputs().path_isdir()) {
      FileUsage fu(ISDIR);
      proc->parent_exec_point()->propagate_file_usage(filename, fu);
    }
    for (const std::string& filename : inouts.inputs().path_notexist_or_isreg_empty()) {
      FileUsage fu(NOTEXIST_OR_ISREG_EMPTY);
      proc->parent_exec_point()->propagate_file_usage(filename, fu);
    }
    for (const std::string& filename : inouts.inputs().path_notexist()) {
      FileUsage fu(NOTEXIST);
      proc->parent_exec_point()->propagate_file_usage(filename, fu);
    }
  }

  /* We'll reuse this for every file modification event to propagate. */
  FileUsage fu;
  fu.set_written(true);

  for (const msg::File& file : inouts.outputs().path_isdir()) {
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   Creating directory: " + pretty_print_string(file.path()));
    mkdir(file.path().c_str(), file.mode());
    if (proc->parent_exec_point()) {
      proc->parent_exec_point()->propagate_file_usage(file.path(), fu);
    }
  }
  for (const msg::File& file : inouts.outputs().path_isreg_with_hash()) {
    FB_DEBUG(FB_DEBUG_SHORTCUT,
             "│   Fetching file from blobs cache: "
             + pretty_print_string(file.path()));
    Hash hash;
    hash.set_hash_from_binary(file.hash());
    cache_->retrieve_file(hash, file.path());
    if (file.has_mode()) {
      /* Refuse to apply setuid, setgid, sticky bit. */
      // FIXME warn on them, even when we store them.
      chmod(file.path().c_str(), file.mode() & 0777);
    }
    if (proc->parent_exec_point()) {
      proc->parent_exec_point()->propagate_file_usage(file.path(), fu);
    }
  }
  /* Walk backwards, so that inner contents are removed before the directory itself. */
  for (int i = inouts.outputs().path_notexist_size() - 1; i >= 0; i--) {
    const std::string& filename = inouts.outputs().path_notexist(i);
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   Deleting file or directory: " + pretty_print_string(filename));
    if (unlink(filename.c_str()) < 0 && errno == EISDIR) {
      rmdir(filename.c_str());
    }
    if (proc->parent_exec_point()) {
      proc->parent_exec_point()->propagate_file_usage(filename, fu);
    }
  }

  /* Set the exit code, propagate upwards. */
  // TODO(egmont) what to do with resource usage?
  proc->exit_result(inouts.outputs().exit_status(), 0, 0);

  return true;
}

/**
 * Tries to shortcut the process.
 *
 * Returns if it succeeded.
 */
bool ExecedProcessCacher::shortcut(ExecedProcess *proc) {
  if (no_fetch_) {
    return false;
  }

  bool ret = false;
  msg::ProcessInputsOutputs *inouts = NULL;

  if (FB_DEBUGGING(FB_DEBUG_SHORTCUT)) {
    FB_DEBUG(FB_DEBUG_SHORTCUT, "┌─");
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│ Trying to shortcut process:");
    if (proc->can_shortcut()) {
      FB_DEBUG(FB_DEBUG_SHORTCUT, "│   fingerprint = " + fingerprints_[proc].to_hex());
    }
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   exe = " + pretty_print_string(proc->executable()));
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   arg = " + pretty_print_array(proc->args()));
    /* FB_DEBUG(FB_DEBUG_SHORTCUT, "│   env = " + pretty_print_array(proc->env_vars())); */
  }

  if (proc->can_shortcut()) {
    inouts = find_shortcut(proc);
  }

  FB_DEBUG(FB_DEBUG_SHORTCUT, inouts ? "│ Shortcutting:" : "│ Not shortcutting.");

  if (inouts) {
    ret = apply_shortcut(proc, *inouts);
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   Exiting with " + std::to_string(proc->exit_status()));
    delete inouts;
  }
  FB_DEBUG(FB_DEBUG_SHORTCUT, "└─");

  proc->set_was_shortcut(ret);
  return ret;
}

}  // namespace firebuild
