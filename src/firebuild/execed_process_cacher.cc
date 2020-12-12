/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include "firebuild/execed_process_cacher.h"
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "firebuild/debug.h"
#include "firebuild/execed_process.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#include "firebuild/cache_object_format_generated.h"
#pragma GCC diagnostic pop
#include "firebuild/file_name.h"
#include "firebuild/hash_cache.h"

namespace firebuild {

// TODO(rbalint) add pretty hash printer for debugging or switch to base64 hash storage format

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
  flatbuffers::FlatBufferBuilder builder(64*1024);

  auto fp_cwd = builder.CreateString(proc->cwd()->c_str(), proc->cwd()->length());
  auto fp_args = builder.CreateVectorOfStrings(proc->args());

  std::vector<flatbuffers::Offset<flatbuffers::String>> fp_env_vec;
  /* Already sorted by the interceptor */
  for (auto& env : proc->env_vars()) {
    if (env_fingerprintable(env)) {
      fp_env_vec.push_back(builder.CreateString(env));
    }
  }
  auto fp_env = builder.CreateVector(fp_env_vec);

  /* The executable and its hash */
  Hash hash;
  if (!hash_cache->get_hash(proc->executable(), &hash)) {
    return false;
  }
  auto file_path = builder.CreateString(proc->executable()->c_str(), proc->executable()->length());
  auto file_hash =
      builder.CreateVector(hash.to_binary(), Hash::hash_size());
  auto fp_executable = msg::CreateFile(builder, file_path, file_hash);

  std::vector<flatbuffers::Offset<msg::File>> fp_libs_vec;
  const auto linux_vdso = FileName::Get("linux-vdso.so.1");
  for (const auto lib : proc->libs()) {
    if (lib == linux_vdso) {
      continue;
    }
    if (!hash_cache->get_hash(lib, &hash)) {
      return false;
    }
    auto lib_path = builder.CreateString(lib->c_str(), lib->length());
    auto lib_hash =
        builder.CreateVector(hash.to_binary(), Hash::hash_size());
    auto fp_lib = msg::CreateFile(builder, lib_path, lib_hash);
    fp_libs_vec.push_back(fp_lib);
  }
  auto fp_libs = builder.CreateVector(fp_libs_vec);

  auto fp = msg::CreateProcessFingerprint(builder, fp_executable, fp_libs, fp_args, fp_env, fp_cwd);
  builder.Finish(fp);
  hash.set_from_data(builder.GetBufferPointer(), builder.GetSize());

  fingerprints_[proc] = hash;
  if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
    std::vector<unsigned char> buf(builder.GetSize());
    memcpy(buf.data(), builder.GetBufferPointer(), builder.GetSize());
    fingerprint_msgs_[proc] = buf;
  }
  return true;
}

void ExecedProcessCacher::erase_fingerprint(const ExecedProcess *proc) {
  fingerprints_.erase(proc);
  if (FB_DEBUGGING(FB_DEBUG_CACHE) && fingerprint_msgs_.count(proc) > 0) {
    fingerprint_msgs_.erase(proc);
  }
}

static std::vector<flatbuffers::Offset<flatbuffers::String>>
fns_to_sorted_offsets(std::vector<const FileName*>* fns, flatbuffers::FlatBufferBuilder* builder) {
  std::vector<flatbuffers::Offset<flatbuffers::String>> ret;
  std::qsort(fns->data(), fns->size(), sizeof(fns->data()[0]),
             reinterpret_cast<int (*)(const void*, const void*)>(FileNamePtrCompare));
  for (const auto& fn : *fns) {
    ret.push_back(builder->CreateString(fn->c_str(), fn->length()));
  }
  return ret;
}

void ExecedProcessCacher::store(const ExecedProcess *proc) {
  if (no_store_) {
    return;
  }

  Hash fingerprint = fingerprints_[proc];

  /* Go through the files the process opened for reading and/or writing.
   * Construct the cache entry parts describing the initial and the final state
   * of them. */
  flatbuffers::FlatBufferBuilder builder(64*1024);

  /* Inputs.*/
  std::vector<flatbuffers::Offset<msg::File>> in_path_isreg_with_hash;
  std::vector<const FileName*> in_path_isreg_fns;
  std::vector<flatbuffers::Offset<msg::File>> in_path_isdir_with_hash;
  std::vector<const FileName*> in_path_isdir_fns,
      in_path_notexist_or_isreg_fns,
      in_path_notexist_or_isreg_empty_fns,
      in_path_notexist_fns;

  /* Outputs.*/
  std::vector<flatbuffers::Offset<msg::File>> out_path_isreg_with_hash,
      out_path_isdir;
  std::vector<const FileName*> out_path_notexist_fns;

  std::vector<file_file_usage> sorted_file_usages;
  for (const auto& pair : proc->file_usages()) {
    const auto filename = pair.first;
    const FileUsage* fu = pair.second;

    /* If the file's initial contents matter, record it in pb's "inputs".
     * This is purely data conversion from one format to another. */
    switch (fu->initial_state()) {
      case DONTKNOW:
        /* Nothing to do. */
        break;
      case ISREG_WITH_HASH: {
        const auto path = builder.CreateString(filename->c_str(), filename->length());
        const auto hash =
            builder.CreateVector(fu->initial_hash().to_binary(), Hash::hash_size());
        in_path_isreg_with_hash.push_back(msg::CreateFile(builder, path, hash));
        break;
      }
      case ISREG:
        in_path_isreg_fns.push_back(filename);
        break;
      case ISDIR_WITH_HASH: {
        const auto path = builder.CreateString(filename->c_str(), filename->length());
        const auto hash =
            builder.CreateVector(fu->initial_hash().to_binary(), Hash::hash_size());
        in_path_isdir_with_hash.push_back(msg::CreateFile(builder, path, hash));
        break;
      }
      case ISDIR:
        in_path_isdir_fns.push_back(filename);
        break;
      case NOTEXIST_OR_ISREG:
        in_path_notexist_or_isreg_fns.push_back(filename);
        break;
      case NOTEXIST_OR_ISREG_EMPTY:
        in_path_notexist_or_isreg_empty_fns.push_back(filename);
        break;
      case NOTEXIST:
        in_path_notexist_fns.push_back(filename);
        break;
      default:
        assert(false);
    }

    /* If the file's final contents matter, place it in the file cache,
     * and also record it in pb's "outputs". This actually needs to
     * compute the checksums now. */
    if (fu->written()) {
      Hash new_hash;
      struct stat st;
      if (stat(filename->c_str(), &st) == 0) {
        if (S_ISREG(st.st_mode)) {
          /* TODO don't store and don't record if it was read with the same hash. */
          if (!cache_->store_file(filename, &new_hash)) {
            /* unexpected error, now what? */
            FB_DEBUG(FB_DEBUG_CACHING, "Could not store blob in cache, not writing shortcut info");
            return;
          }
          const auto path = builder.CreateString(filename->c_str(), filename->length());
          const auto hash =
              builder.CreateVector(new_hash.to_binary(), Hash::hash_size());
          // TODO(egmont) fail if setuid/setgid/sticky is set
          /* File's default values. */
          const int mtime = 0, size = 0;
          int mode = st.st_mode & 07777;
          out_path_isreg_with_hash.push_back(msg::CreateFile(builder, path, hash, mtime, size,
                                                             mode));
        } else if (S_ISDIR(st.st_mode)) {
          const auto path = builder.CreateString(filename->c_str(), filename->length());
          /* File's default values. */
          const int hash = 0, mtime = 0, size = 0;
          // TODO(egmont) fail if setuid/setgid/sticky is set
          const int mode = st.st_mode & 07777;
          out_path_isdir.push_back(msg::CreateFile(builder, path, hash, mtime, size, mode));
        } else {
          // TODO(egmont) handle other types of entries
        }
      } else {
        if (fu->initial_state() != NOTEXIST) {
          out_path_notexist_fns.push_back(filename);
        }
      }
    }
  }

  auto in_path_isreg = fns_to_sorted_offsets(&in_path_isreg_fns, &builder);
  auto in_path_isdir = fns_to_sorted_offsets(&in_path_isdir_fns, &builder);
  auto in_path_notexist_or_isreg =
      fns_to_sorted_offsets(&in_path_notexist_or_isreg_fns, &builder);
  auto in_path_notexist_or_isreg_empty =
      fns_to_sorted_offsets(&in_path_notexist_or_isreg_empty_fns, &builder);
  auto in_path_notexist = fns_to_sorted_offsets(&in_path_notexist_fns, &builder);
  auto out_path_notexist = fns_to_sorted_offsets(&out_path_notexist_fns, &builder);

  auto inputs =
      msg::CreateProcessInputs(builder,
                               builder.CreateVectorOfSortedTables(&in_path_isreg_with_hash),
                               builder.CreateVector(in_path_isreg),
                               builder.CreateVectorOfSortedTables(&in_path_isdir_with_hash),
                               builder.CreateVector(in_path_isdir),
                               builder.CreateVector(in_path_notexist_or_isreg),
                               builder.CreateVector(in_path_notexist_or_isreg_empty),
                               builder.CreateVector(in_path_notexist));
  auto outputs =
      msg::CreateProcessOutputs(builder,
                                builder.CreateVectorOfSortedTables(&out_path_isreg_with_hash),
                                builder.CreateVectorOfSortedTables(&out_path_isdir),
                                builder.CreateVector(out_path_notexist),
                                proc->exit_status());
  // TODO(egmont) Add all sorts of other stuff

  auto pio = msg::CreateProcessInputsOutputs(builder, inputs, outputs);
  builder.Finish(pio);

  uint8_t *debug_msg = NULL;
  if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
    debug_msg = fingerprint_msgs_[proc].data();
  }

  /* Store in the cache everything about this process. */
  multi_cache_->store(fingerprint, builder.GetBufferPointer(), builder.GetSize(), debug_msg, NULL);
}

/**
 * Check whether the given ProcessInputs matches the file system's
 * current contents.
 */
static bool pi_matches_fs(const msg::ProcessInputs& pi, const Hash& fingerprint) {
  struct stat64 st;
  for (const auto& file : *pi.path_isreg_with_hash()) {
    Hash on_fs_hash, in_cache_hash;
    bool is_dir;
    const auto path = FileName::Get(file->path());
    if (!hash_cache->get_hash(path, &on_fs_hash, &is_dir) || is_dir) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   " + fingerprint.to_ascii()
               + " mismatches e.g. at " + pretty_print_string(path)
               + ": regular file expected but does not exist or something else found");
      return false;
    }
    assert(file->hash()->size() == Hash::hash_size());
    in_cache_hash.set_hash_from_binary(file->hash()->data());
    if (on_fs_hash != in_cache_hash) {
      FB_DEBUG(FB_DEBUG_SHORTCUT, "│   " + fingerprint.to_ascii() + " mismatches e.g. at " +
                                  pretty_print_string(path) + ": hash differs");
      return false;
    }
  }
  for (const auto& file : *pi.path_isdir_with_hash()) {
    Hash on_fs_hash, in_cache_hash;
    bool is_dir;
    const auto path = FileName::Get(file->path());
    if (!hash_cache->get_hash(path, &on_fs_hash, &is_dir) || !is_dir) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   " + fingerprint.to_ascii()
               + " mismatches e.g. at " + pretty_print_string(path)
               + ": directory expected but does not exist or something else found");
      return false;
    }
    assert(file->hash()->size() == Hash::hash_size());
    in_cache_hash.set_hash_from_binary(file->hash()->data());
    if (on_fs_hash != in_cache_hash) {
      FB_DEBUG(FB_DEBUG_SHORTCUT, "│   " + fingerprint.to_ascii() + " mismatches e.g. at " +
                                  pretty_print_string(path) + ": hash differs");
      return false;
    }
  }
  for (const auto& filename : *pi.path_isreg()) {
    if (stat64(filename->c_str(), &st) == -1 || !S_ISREG(st.st_mode)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   " + fingerprint.to_ascii()
               + " mismatches e.g. at " + pretty_print_string(filename->str())
               + ": regular file expected but does not exist or something else found");
      return false;
    }
  }
  for (const auto& filename : *pi.path_isdir()) {
    if (stat64(filename->c_str(), &st) == -1 || !S_ISDIR(st.st_mode)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   " + fingerprint.to_ascii()
               + " mismatches e.g. at " + pretty_print_string(filename->str())
               + ": directory expected but does not exist or something else found");
      return false;
    }
  }
  for (const auto& filename : *pi.path_notexist_or_isreg()) {
    if (stat64(filename->c_str(), &st) != -1 && !S_ISREG(st.st_mode)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   " + fingerprint.to_ascii()
               + " mismatches e.g. at " + pretty_print_string(filename->str())
               + ": file expected to be missing or regular, something else found");
      return false;
    }
  }
  for (const auto& filename : *pi.path_notexist_or_isreg_empty()) {
    if (stat64(filename->c_str(), &st) != -1 && (!S_ISREG(st.st_mode) || st.st_size > 0)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   " + fingerprint.to_ascii()
               + " mismatches e.g. at " + pretty_print_string(filename->str())
               + ": file expected to be missing or empty, non-empty file or something else found");
      return false;
    }
  }
  for (const auto& filename : *pi.path_notexist()) {
    if (stat64(filename->c_str(), &st) != -1) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   " + fingerprint.to_ascii()
               + " mismatches e.g. at " + pretty_print_string(filename->str())
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
const msg::ProcessInputsOutputs* ExecedProcessCacher::find_shortcut(const ExecedProcess *proc,
                                                                    uint8_t **inouts_buf,
                                                                    size_t *inouts_buf_len) {
  const msg::ProcessInputsOutputs *ret = NULL;
  int count = 0;
  Hash fingerprint = fingerprints_[proc];  // FIXME error handling

  FB_DEBUG(FB_DEBUG_SHORTCUT, "│ Candidates:");
  std::vector<Hash> subkeys = multi_cache_->list_subkeys(fingerprint);
  if (subkeys.empty()) {
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   None found");
  }
  for (const Hash& subkey : subkeys) {
    if (!multi_cache_->retrieve(fingerprint, subkey, inouts_buf, inouts_buf_len)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   Cannot retrieve " + subkey.to_ascii() + " from multicache, ignoring");
      continue;
    }
    auto inouts = msg::GetProcessInputsOutputs(*inouts_buf);
    if (!inouts->inputs() || pi_matches_fs(*inouts->inputs(), subkey)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT, "│   " + subkey.to_ascii() + " matches the file system");
      count++;
      if (count == 1) {
        ret = inouts;
        /* Let's play safe for now and not break out of this loop, let's
         * make sure that there are no other matches. */
      }
      if (count == 2) {
        FB_DEBUG(FB_DEBUG_SHORTCUT,
                 "│   More than 1 matching candidates found, ignoring them all");
        inouts = nullptr;
        munmap(*inouts_buf, *inouts_buf_len);
        return NULL;
      }
    } else {
      inouts = nullptr;
      munmap(*inouts_buf, *inouts_buf_len);
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
                                         const msg::ProcessInputsOutputs* const inouts) {
  if (proc->parent_exec_point()) {
    for (const auto& file : *inouts->inputs()->path_isreg_with_hash()) {
      Hash hash;
      assert(file->hash()->size() == Hash::hash_size());
      hash.set_hash_from_binary(file->hash()->data());
      FileUsage fu(ISREG_WITH_HASH, hash);
      const auto path = FileName::Get(file->path());
      proc->parent_exec_point()->propagate_file_usage(path, fu);
    }
    for (const auto& file : *inouts->inputs()->path_isdir_with_hash()) {
      Hash hash;
      assert(file->hash()->size() == Hash::hash_size());
      hash.set_hash_from_binary(file->hash()->data());
      FileUsage fu(ISDIR_WITH_HASH, hash);
      const auto path = FileName::Get(file->path());
      proc->parent_exec_point()->propagate_file_usage(path, fu);
    }
    for (const auto& filename : *inouts->inputs()->path_isreg()) {
      FileUsage fu(ISREG);
      proc->parent_exec_point()->propagate_file_usage(FileName::Get(filename), fu);
    }
    for (const auto& filename : *inouts->inputs()->path_isdir()) {
      FileUsage fu(ISDIR);
      proc->parent_exec_point()->propagate_file_usage(FileName::Get(filename), fu);
    }
    for (const auto& filename : *inouts->inputs()->path_notexist_or_isreg()) {
      FileUsage fu(NOTEXIST_OR_ISREG);
      proc->parent_exec_point()->propagate_file_usage(FileName::Get(filename), fu);
    }
    for (const auto& filename : *inouts->inputs()->path_notexist_or_isreg_empty()) {
      FileUsage fu(NOTEXIST_OR_ISREG_EMPTY);
      proc->parent_exec_point()->propagate_file_usage(FileName::Get(filename), fu);
    }
    for (const auto& filename : *inouts->inputs()->path_notexist()) {
      FileUsage fu(NOTEXIST);
      proc->parent_exec_point()->propagate_file_usage(FileName::Get(filename), fu);
    }
  }

  /* We'll reuse this for every file modification event to propagate. */
  FileUsage fu;
  fu.set_written(true);

  for (const auto& file : *inouts->outputs()->path_isdir()) {
    const auto path = FileName::Get(file->path());
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   Creating directory: " + pretty_print_string(path));
    assert(file->mode() != -1);
    mkdir(path->c_str(), file->mode());
    if (proc->parent_exec_point()) {
      proc->parent_exec_point()->propagate_file_usage(path, fu);
    }
  }
  for (const auto& file : *inouts->outputs()->path_isreg_with_hash()) {
    const auto path = FileName::Get(file->path());
    FB_DEBUG(FB_DEBUG_SHORTCUT,
             "│   Fetching file from blobs cache: "
             + pretty_print_string(path));
    Hash hash;
    assert(file->hash()->size() == Hash::hash_size());
    hash.set_hash_from_binary(file->hash()->data());
    cache_->retrieve_file(hash, path);
    /* mode is -1 by default in flatbuffers */
    if (file->mode() != -1) {
      /* Refuse to apply setuid, setgid, sticky bit. */
      // FIXME warn on them, even when we store them.
      chmod(path->c_str(), file->mode() & 0777);
    }
    if (proc->parent_exec_point()) {
      proc->parent_exec_point()->propagate_file_usage(path, fu);
    }
  }
  /* Walk backwards, so that inner contents are removed before the directory itself. */
  for (int i = inouts->outputs()->path_notexist()->size() - 1; i >= 0; i--) {
    const auto filename = FileName::Get(inouts->outputs()->path_notexist()->Get(i));
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   Deleting file or directory: " + pretty_print_string(filename));
    if (unlink(filename->c_str()) < 0 && errno == EISDIR) {
      rmdir(filename->c_str());
    }
    if (proc->parent_exec_point()) {
      proc->parent_exec_point()->propagate_file_usage(filename, fu);
    }
  }

  /* Set the exit code, propagate upwards. */
  // TODO(egmont) what to do with resource usage?
  proc->exit_result(inouts->outputs()->exit_status(), 0, 0);

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
  uint8_t * inouts_buf = NULL;
  size_t inouts_buf_len = 0;
  const msg::ProcessInputsOutputs *inouts = NULL;

  if (FB_DEBUGGING(FB_DEBUG_SHORTCUT)) {
    FB_DEBUG(FB_DEBUG_SHORTCUT, "┌─");
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│ Trying to shortcut process:");
    if (proc->can_shortcut()) {
      FB_DEBUG(FB_DEBUG_SHORTCUT, "│   fingerprint = " + fingerprints_[proc].to_ascii());
    }
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   exe = " + pretty_print_string(proc->executable()));
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   arg = " + pretty_print_array(proc->args()));
    /* FB_DEBUG(FB_DEBUG_SHORTCUT, "│   env = " + pretty_print_array(proc->env_vars())); */
  }

  if (proc->can_shortcut()) {
    inouts = find_shortcut(proc, &inouts_buf, &inouts_buf_len);
  }

  FB_DEBUG(FB_DEBUG_SHORTCUT, inouts ? "│ Shortcutting:" : "│ Not shortcutting.");

  if (inouts) {
    ret = apply_shortcut(proc, inouts);
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   Exiting with " + std::to_string(proc->exit_status()));
    /* Trigger cleanup of ProcessInputsOutputs. */
    inouts = nullptr;
    munmap(inouts_buf, inouts_buf_len);
  }
  FB_DEBUG(FB_DEBUG_SHORTCUT, "└─");

  proc->set_was_shortcut(ret);
  return ret;
}

}  // namespace firebuild
