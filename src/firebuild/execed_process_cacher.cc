/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include "firebuild/execed_process_cacher.h"
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "firebuild/debug.h"
#include "firebuild/execed_process.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#include "firebuild/cache_object_format_generated.h"
#pragma GCC diagnostic pop
#include "firebuild/file_name.h"
#include "firebuild/hash_cache.h"
#include "firebuild/hashed_flatbuffers_file_vector.h"
#include "firebuild/hashed_flatbuffers_string_vector.h"

namespace firebuild {

static const XXH64_hash_t kFingerprintVersion = 0;

// TODO(rbalint) add pretty hash printer for debugging or switch to base64 hash storage format

/**
 * One object is responsible for handling the fingerprinting and caching
 * of multiple ExecedProcesses which potentially come from / go to the
 * same cache.
 */
ExecedProcessCacher::ExecedProcessCacher(bool no_store,
                                         bool no_fetch,
                                         const libconfig::Setting& envs_skip) :
    no_store_(no_store), no_fetch_(no_fetch),
    envs_skip_(), fingerprints_(), fingerprint_msgs_() {
  for (int i = 0; i < envs_skip.getLength(); i++) {
    envs_skip_.insert(envs_skip[i]);
  }
}

/**
 * Helper for fingerprint() to decide which env vars matter
 */
bool ExecedProcessCacher::env_fingerprintable(const std::string& name_and_value) const {
  /* Strip off the "=value" part. */
  const std::string name = name_and_value.substr(0, name_and_value.find('='));

  /* Env vars to skip, taken from the config files.
   * Note: FB_SOCKET is already filtered out in the interceptor. */
  return envs_skip_.find(name) == envs_skip_.end();
}

/**
 * Add file_name to fingerprint, including the ending '\0'.
 *
 * Adding the ending '\0' prevents hash collisions by concatenating filenames.
 */
static void add_to_hash_state(XXH3_state_t* state, const FileName* file_name) {
  if (XXH3_128bits_update(state, file_name->c_str(), file_name->length() + 1) == XXH_ERROR) {
    abort();
  }
}

/**
 * Add string to fingerprint, including the ending '\0'.
 *
 * Adding the ending '\0' prevents hash collisions by concatenating strings.
 */
static void add_to_hash_state(XXH3_state_t* state, const std::string& str) {
  if (XXH3_128bits_update(state, str.c_str(), str.length() + 1) == XXH_ERROR) {
    abort();
  }
}

/**
 * Add hash to fingerprint
 */
static void add_to_hash_state(XXH3_state_t* state, const Hash& hash) {
  if (XXH3_128bits_update(state, hash.to_binary(), Hash::hash_size()) == XXH_ERROR) {
    abort();
  }
}
static void add_to_hash_state(XXH3_state_t* state, const XXH128_hash_t& hash) {
  if (XXH3_128bits_update(state, &hash, sizeof(XXH128_hash_t)) == XXH_ERROR) {
    abort();
  }
}

/**
 * Add int to fingerprint
 */
static void add_to_hash_state(XXH3_state_t* state, const int i) {
  if (XXH3_128bits_update(state, &i, sizeof(int)) == XXH_ERROR) {
    abort();
  }
}

static Hash state_to_hash(XXH3_state_t* state) {
  const XXH128_hash_t digest = XXH3_128bits_digest(state);
  uint8_t canonical_digest[Hash::hash_size()];
  /* Convert from endian-specific representation to endian-independent byte array. */
  XXH128_canonicalFromHash(reinterpret_cast<XXH128_canonical_t *>(&canonical_digest), digest);
  return Hash(canonical_digest);
}

/**
 * Compute the fingerprint, store it keyed by the process in fingerprints_.
 * Also store fingerprint_msgs_ if debugging is enabled.
 */
bool ExecedProcessCacher::fingerprint(const ExecedProcess *proc) {
  TRACK(FB_DEBUG_PROC, "proc=%s", D(proc));

  XXH3_state_t state;
  if (XXH3_128bits_reset_withSeed(&state, kFingerprintVersion) == XXH_ERROR) {
    abort();
  }
  add_to_hash_state(&state, proc->initial_wd());
  /* Size is added to not allow collisions between elements of different containers.
   * Otherwise "cmd foo BAR=1" would collide with "env BAR=1 cmd foo". */
  add_to_hash_state(&state, proc->args().size());
  for (const auto& arg : proc->args()) {
    add_to_hash_state(&state, arg);
  }

  /* Already sorted by the interceptor */
  add_to_hash_state(&state, proc->env_vars().size());
  for (const auto& env : proc->env_vars()) {
    if (env_fingerprintable(env)) {
      add_to_hash_state(&state, env);
    }
  }

  /* The executable and its hash */
  add_to_hash_state(&state, proc->executable());
  Hash hash;
  if (!hash_cache->get_hash(proc->executable(), &hash)) {
    return false;
  }
  add_to_hash_state(&state, hash);

  if (proc->executable() == proc->executed_path()) {
    add_to_hash_state(&state, proc->executable());
    add_to_hash_state(&state, hash);
  } else {
    add_to_hash_state(&state, proc->executed_path());
    if (!hash_cache->get_hash(proc->executed_path(), &hash)) {
      return false;
    }
    add_to_hash_state(&state, hash);
  }

  const auto linux_vdso = FileName::Get("linux-vdso.so.1");
  add_to_hash_state(&state, proc->libs().size());
  for (const auto lib : proc->libs()) {
    if (lib == linux_vdso) {
      continue;
    }
    if (!hash_cache->get_hash(lib, &hash)) {
      return false;
    }
    add_to_hash_state(&state, lib);
    add_to_hash_state(&state, hash);
  }

  /* The inherited pipes */
  for (const inherited_pipe_t& inherited_pipe : proc->inherited_pipes()) {
    for (int fd : inherited_pipe.fds) {
      add_to_hash_state(&state, fd);
    }
    /* Close each inherited pipe with and invalid value to avoid collisions. */
    add_to_hash_state(&state, -1);
  }

  fingerprints_[proc] = state_to_hash(&state);

  if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
    flatbuffers::FlatBufferBuilder builder(64*1024);

    auto fp_wd = builder.CreateString(proc->initial_wd()->c_str(), proc->initial_wd()->length());
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
    if (!hash_cache->get_hash(proc->executable(), &hash)) {
      return false;
    }
    auto file_path = builder.CreateString(proc->executable()->c_str(),
                                          proc->executable()->length());
    auto file_hash =
        builder.CreateVector(hash.to_binary(), Hash::hash_size());
    auto fp_executable = msg::CreateFile(builder, file_path, file_hash);

    flatbuffers::Offset<firebuild::msg::File> fp_executed_path;
    if (proc->executable() == proc->executed_path()) {
      /* Those often match, don't create the same string twice. */
      fp_executed_path = fp_executable;
    } else {
      auto executed_file_path = builder.CreateString(proc->executed_path()->c_str(),
                                                     proc->executed_path()->length());
      if (!hash_cache->get_hash(proc->executed_path(), &hash)) {
        return false;
      }
      auto executed_file_hash =
          builder.CreateVector(hash.to_binary(), Hash::hash_size());
      fp_executed_path = msg::CreateFile(builder, executed_file_path, executed_file_hash);
    }

    /* The linked libraries */
    std::vector<flatbuffers::Offset<msg::File>> fp_libs_vec;
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

    /* The inherited pipes */
    std::vector<flatbuffers::Offset<msg::PipeFds>> fp_pipefds_vec;
    for (const inherited_pipe_t& inherited_pipe : proc->inherited_pipes()) {
      std::vector<int> fds;
      for (int fd : inherited_pipe.fds) {
        fds.push_back(fd);
      }
      auto fp_fds = builder.CreateVector(fds);
      auto fp_pipefds = msg::CreatePipeFds(builder, fp_fds);
      fp_pipefds_vec.push_back(fp_pipefds);
    }
    auto fp_pipefds = builder.CreateVector(fp_pipefds_vec);

    auto fp = msg::CreateProcessFingerprint(builder, fp_executable, fp_executed_path, fp_libs,
                                            fp_args, fp_env, fp_wd, fp_pipefds);
    builder.Finish(fp);

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

void sort_and_add_to_hash_state(HashedFlatbuffersStringVector* hashed_vec,
                                XXH3_state_t* hash_state) {
  hashed_vec->sort_hashes();
  add_to_hash_state(hash_state, hashed_vec->hash());
}

void sort_and_add_to_hash_state(HashedFlatbuffersFileVector* hashed_vec, XXH3_state_t* hash_state) {
  hashed_vec->sort_hashes();
  add_to_hash_state(hash_state, hashed_vec->hash());
}

void ExecedProcessCacher::store(const ExecedProcess *proc) {
  TRACK(FB_DEBUG_PROC, "proc=%s", D(proc));

  if (no_store_) {
    /* This is when FIREBUILD_READONLY is set. We could have decided not to create PipeRecorders
     * at all. But maybe go with the default code path, i.e. record the data to temporary files,
     * but at the last step purge them instead of moving them to their final location in the cache.
     * This way the code path is more similar to the regular case. */
    for (const inherited_pipe_t& inherited_pipe : proc->inherited_pipes()) {
      if (inherited_pipe.recorder) {
        inherited_pipe.recorder->abandon();
      }
    }
    return;
  }

  Hash fingerprint = fingerprints_[proc];
  XXH3_state_t inouts_hash_state;
  if (XXH3_128bits_reset_withSeed(&inouts_hash_state, kFingerprintVersion) == XXH_ERROR) {
    abort();
  }

  /* Go through the files the process opened for reading and/or writing.
   * Construct the cache entry parts describing the initial and the final state
   * of them. */
  flatbuffers::FlatBufferBuilder builder(64*1024);

  /* Inputs.*/
  HashedFlatbuffersFileVector in_path_isreg_with_hash(&builder),
      in_system_path_isreg_with_hash(&builder),
      in_path_isdir_with_hash(&builder),
      in_system_path_isdir_with_hash(&builder);
  HashedFlatbuffersStringVector in_path_isreg(&builder),
      in_path_isdir(&builder),
      in_path_notexist_or_isreg(&builder),
      in_path_notexist_or_isreg_empty(&builder),
      in_path_notexist(&builder);

  /* Outputs.*/
  HashedFlatbuffersFileVector out_path_isreg_with_hash(&builder),
      out_path_isdir(&builder);
  HashedFlatbuffersStringVector out_path_notexist(&builder);
  std::vector<flatbuffers::Offset<msg::PipeData>> out_pipe_data;

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
        if (filename->is_in_system_location()) {
          in_system_path_isreg_with_hash.add(filename, fu);
        } else {
          in_path_isreg_with_hash.add(filename, fu);
        }
        break;
      }
      case ISREG:
        in_path_isreg.add(filename);
        break;
      case ISDIR_WITH_HASH: {
        if (filename->is_in_system_location()) {
          in_system_path_isdir_with_hash.add(filename, fu);
        } else {
          in_path_isdir_with_hash.add(filename, fu);
        }
        break;
      }
      case ISDIR:
        in_path_isdir.add(filename);
        break;
      case NOTEXIST_OR_ISREG:
        in_path_notexist_or_isreg.add(filename);
        break;
      case NOTEXIST_OR_ISREG_EMPTY:
        in_path_notexist_or_isreg_empty.add(filename);
        break;
      case NOTEXIST:
        in_path_notexist.add(filename);
        break;
      default:
        assert(false);
    }

    /* If the file's final contents matter, place it in the file cache,
     * and also record it in pb's "outputs". This actually needs to
     * compute the checksums now. */
    if (fu->written()) {
      int fd = open(filename->c_str(), O_RDONLY);
      if (fd >= 0) {
        Hash new_hash;
        struct stat64 st;
        if (fstat64(fd, &st) == 0) {
          if (S_ISREG(st.st_mode)) {
            /* TODO don't store and don't record if it was read with the same hash. */
            if (!hash_cache->store_and_get_hash(filename, &new_hash, fd, &st)) {
              /* unexpected error, now what? */
              FB_DEBUG(FB_DEBUG_CACHING,
                       "Could not store blob in cache, not writing shortcut info");
              return;
            }
            // TODO(egmont) fail if setuid/setgid/sticky is set
            int mode = st.st_mode & 07777;
            out_path_isreg_with_hash.add(filename, new_hash, mode);
          } else if (S_ISDIR(st.st_mode)) {
            // TODO(egmont) fail if setuid/setgid/sticky is set
            const int mode = st.st_mode & 07777;
            out_path_isdir.add(filename, mode);
          } else {
            // TODO(egmont) handle other types of entries
          }
        } else {
          perror("fstat");
          if (fu->initial_state() != NOTEXIST) {
            out_path_notexist.add(filename);
          }
        }
        close(fd);
      } else {
        if (fu->initial_state() != NOTEXIST) {
          out_path_notexist.add(filename);
        }
      }
    }
  }

  /* Store what was written to the inherited pipes. Use the fd as of when the process started up,
   * because this is what matters if we want to replay; how the process later dup()ed it to other
   * fds is irrelevant. Similarly, no need to store the data written to pipes opened by this
   * process, that data won't ever be replayed. */
  for (const inherited_pipe_t& inherited_pipe : proc->inherited_pipes()) {
    /* Record the output as belonging to the lowest fd. */
    int fd = inherited_pipe.fds[0];
    std::shared_ptr<PipeRecorder> recorder = inherited_pipe.recorder;
    if (recorder) {
      bool is_empty;
      Hash pipe_traffic_hash;
      if (!recorder->store(&is_empty, &pipe_traffic_hash)) {
        // FIXME handle error
        FB_DEBUG(FB_DEBUG_CACHING,
                 "Could not store pipe traffic in cache, not writing shortcut info");
        return;
      }
      if (!is_empty) {
        /* Note: pipes with no traffic are just simply not mentioned here in the "outputs" section.
         * They were taken into account when computing the process's fingerprint. */
        const auto hash =
            builder.CreateVector(pipe_traffic_hash.to_binary(), Hash::hash_size());
        out_pipe_data.push_back(msg::CreatePipeData(builder, fd, hash));
        add_to_hash_state(&inouts_hash_state, fd);
        add_to_hash_state(&inouts_hash_state, pipe_traffic_hash);
      }
    }
  }

  sort_and_add_to_hash_state(&in_path_isreg, &inouts_hash_state);
  sort_and_add_to_hash_state(&in_path_isreg_with_hash, &inouts_hash_state);
  sort_and_add_to_hash_state(&in_system_path_isreg_with_hash, &inouts_hash_state);
  sort_and_add_to_hash_state(&in_path_isdir, &inouts_hash_state);
  sort_and_add_to_hash_state(&in_path_isdir_with_hash, &inouts_hash_state);
  sort_and_add_to_hash_state(&in_system_path_isdir_with_hash, &inouts_hash_state);
  sort_and_add_to_hash_state(&in_path_notexist_or_isreg, &inouts_hash_state);
  sort_and_add_to_hash_state(&in_path_notexist_or_isreg_empty, &inouts_hash_state);
  sort_and_add_to_hash_state(&in_path_notexist, &inouts_hash_state);
  sort_and_add_to_hash_state(&out_path_isreg_with_hash, &inouts_hash_state);
  sort_and_add_to_hash_state(&out_path_isdir, &inouts_hash_state);
  sort_and_add_to_hash_state(&out_path_notexist, &inouts_hash_state);

  auto inputs =
      msg::CreateProcessInputs(builder,
                               builder.CreateVectorOfSortedTables(&in_path_isreg_with_hash.files()),
                               builder.CreateVectorOfSortedTables(
                                   &in_system_path_isreg_with_hash.files()),
                               builder.CreateVector(in_path_isreg.strings()),
                               builder.CreateVectorOfSortedTables(&in_path_isdir_with_hash.files()),
                               builder.CreateVectorOfSortedTables(
                                   &in_system_path_isdir_with_hash.files()),
                               builder.CreateVector(in_path_isdir.strings()),
                               builder.CreateVector(in_path_notexist_or_isreg.strings()),
                               builder.CreateVector(in_path_notexist_or_isreg_empty.strings()),
                               builder.CreateVector(in_path_notexist.strings()));
  auto outputs =
      msg::CreateProcessOutputs(builder,
                                builder.CreateVector(out_path_isreg_with_hash.files()),
                                builder.CreateVector(out_path_isdir.files()),
                                builder.CreateVector(out_path_notexist.strings()),
                                builder.CreateVector(out_pipe_data),
                                proc->exit_status());

  // TODO(egmont) Add all sorts of other stuff

  auto pio = msg::CreateProcessInputsOutputs(builder, inputs, outputs);
  builder.Finish(pio);

  uint8_t *debug_msg = NULL;
  if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
    debug_msg = fingerprint_msgs_[proc].data();
  }

  /* Store in the cache everything about this process. */
  Hash subkey = state_to_hash(&inouts_hash_state);
  obj_cache->store(fingerprint, builder.GetBufferPointer(), builder.GetSize(), debug_msg, subkey);
}

static bool pis_hash_match_fs(
    const flatbuffers::Vector<flatbuffers::Offset<firebuild::msg::File> >* pis, bool is_dir,
    const Hash& fingerprint) {
  for (const auto& file : *pis) {
    Hash on_fs_hash, in_cache_hash;
    bool on_fs_is_dir = false;
    const auto path = FileName::Get(file->path());
    if (!hash_cache->get_hash(path, &on_fs_hash, &on_fs_is_dir) || (is_dir != on_fs_is_dir)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   " + d(fingerprint)
               + " mismatches e.g. at " + d(path)
               + ": regular file expected but does not exist or something else found");
      return false;
    }
    assert_cmp(file->hash()->size(), ==, Hash::hash_size());
    in_cache_hash.set_hash_from_binary(file->hash()->data());
    if (on_fs_hash != in_cache_hash) {
      FB_DEBUG(FB_DEBUG_SHORTCUT, "│   " + d(fingerprint) + " mismatches e.g. at " +
               d(path) + ": hash differs");
      return false;
    }
  }
  return true;
}

/**
 * Check whether the given ProcessInputs matches the file system's
 * current contents.
 */
static bool pi_matches_fs(const msg::ProcessInputs& pi, const Hash& fingerprint) {
  TRACK(FB_DEBUG_PROC, "fingerprint=%s", D(fingerprint));

  struct stat64 st;
  if (!pis_hash_match_fs(pi.path_isreg_with_hash(), false, fingerprint)) {
    return false;
  }
  if (!pis_hash_match_fs(pi.path_isdir_with_hash(), true, fingerprint)) {
    return false;
  }
  for (const auto& filename : *pi.path_isreg()) {
    if (stat64(filename->c_str(), &st) == -1 || !S_ISREG(st.st_mode)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   " + d(fingerprint)
               + " mismatches e.g. at " + d(filename->str())
               + ": regular file expected but does not exist or something else found");
      return false;
    }
  }
  for (const auto& filename : *pi.path_isdir()) {
    if (stat64(filename->c_str(), &st) == -1 || !S_ISDIR(st.st_mode)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   " + d(fingerprint)
               + " mismatches e.g. at " + d(filename->str())
               + ": directory expected but does not exist or something else found");
      return false;
    }
  }
  if (!pis_hash_match_fs(pi.system_path_isreg_with_hash(), false, fingerprint)) {
    return false;
  }
  if (!pis_hash_match_fs(pi.system_path_isdir_with_hash(), true, fingerprint)) {
    return false;
  }
  for (const auto& filename : *pi.path_notexist_or_isreg()) {
    if (stat64(filename->c_str(), &st) != -1 && !S_ISREG(st.st_mode)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   " + d(fingerprint)
               + " mismatches e.g. at " + d(filename->str())
               + ": file expected to be missing or regular, something else found");
      return false;
    }
  }
  for (const auto& filename : *pi.path_notexist_or_isreg_empty()) {
    if (stat64(filename->c_str(), &st) != -1 && (!S_ISREG(st.st_mode) || st.st_size > 0)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   " + d(fingerprint)
               + " mismatches e.g. at " + d(filename->str())
               + ": file expected to be missing or empty, non-empty file or something else found");
      return false;
    }
  }
  for (const auto& filename : *pi.path_notexist()) {
    if (stat64(filename->c_str(), &st) != -1) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   " + d(fingerprint)
               + " mismatches e.g. at " + d(filename->str())
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
  TRACK(FB_DEBUG_PROC, "proc=%s", D(proc));

  const msg::ProcessInputsOutputs *inouts = nullptr;
  int count = 0;
  Hash fingerprint = fingerprints_[proc];  // FIXME error handling

  FB_DEBUG(FB_DEBUG_SHORTCUT, "│ Candidates:");
  std::vector<Hash> subkeys = obj_cache->list_subkeys(fingerprint);
  if (subkeys.empty()) {
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   None found");
  }
  for (const Hash& subkey : subkeys) {
    uint8_t *candidate_inouts_buf;
    size_t candidate_inouts_buf_len;
    if (!obj_cache->retrieve(fingerprint, subkey,
                             &candidate_inouts_buf, &candidate_inouts_buf_len)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   Cannot retrieve " + d(subkey) + " from objcache, ignoring");
      continue;
    }
    const msg::ProcessInputsOutputs *candidate_inouts =
        msg::GetProcessInputsOutputs(candidate_inouts_buf);
    if (!candidate_inouts->inputs() || pi_matches_fs(*candidate_inouts->inputs(), subkey)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT, "│   " + d(subkey) + " matches the file system");
      count++;
      if (count == 1) {
        *inouts_buf = candidate_inouts_buf;
        *inouts_buf_len = candidate_inouts_buf_len;
        inouts = candidate_inouts;
        /* Let's play safe for now and not break out of this loop, let's
         * make sure that there are no other matches. */
      }
      if (count == 2) {
        FB_DEBUG(FB_DEBUG_SHORTCUT,
                 "│   More than 1 matching candidates found, ignoring them all");
        munmap(candidate_inouts_buf, candidate_inouts_buf_len);
        return nullptr;
      }
    } else {
      munmap(candidate_inouts_buf, candidate_inouts_buf_len);
    }
  }
  return inouts;
}

/**
 * Restore output directories starting with start_index in sorted order.
 */
static bool restore_remaining_dirs_sorted(
    ExecedProcess* proc,
    const flatbuffers::Vector<flatbuffers::Offset<firebuild::msg::File> >* dirs,
    const FileUsage* fu, int start_index) {
  int index = 0;
  std::vector<std::pair<const FileName*, int>> remaining_dirs;
  for (const auto& file : *dirs) {
    if (index++ < start_index) {
      continue;
    }
    remaining_dirs.push_back({FileName::Get(file->path()), file->mode()});
  }

  struct {
    bool operator()(const std::pair<const FileName*, int>& d1,
                    const std::pair<const FileName*, int>& d2) const {
      return strcmp(d1.first->c_str(), d2.first->c_str()) < 0;
      }
  } remaining_dirs_less;
  std::sort(remaining_dirs.begin(), remaining_dirs.end(), remaining_dirs_less);

  for (auto&& [path, mode] : remaining_dirs) {
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   Creating directory: " + d(path));
    assert_cmp(mode, !=, -1);
    int ret = mkdir(path->c_str(), mode);
    if (ret != 0) {
      perror("Failed to restore directory");
      assert_cmp(ret, !=, -1);
      return false;
    }
    if (proc->parent_exec_point()) {
      proc->parent_exec_point()->propagate_file_usage(path, fu);
    }
  }
  return true;
}

/**
 * Restore output directories with the right mode.
 */
static bool restore_dirs(
    ExecedProcess* proc,
    const flatbuffers::Vector<flatbuffers::Offset<firebuild::msg::File> >* dirs,
    const FileUsage* fu) {
  int index = 0;
  for (const auto& file : *dirs) {
    const auto path = FileName::Get(file->path());
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   Creating directory: " + d(path));
    assert_cmp(file->mode(), !=, -1);
    int ret = mkdir(path->c_str(), file->mode());
    if (ret != 0) {
      if (errno == ENOENT) {
        return restore_remaining_dirs_sorted(proc, dirs, fu, index);
      }
      perror("Failed to restore directory");
      assert_cmp(ret, !=, -1);
      return false;
    }
    if (proc->parent_exec_point()) {
      proc->parent_exec_point()->propagate_file_usage(path, fu);
    }
    index++;
  }
  return true;
}

/**
 * Applies the given shortcut.
 *
 * Modifies the file system to match the given instructions. Propagates
 * upwards all the shortcutted file read and write events.
 */
bool ExecedProcessCacher::apply_shortcut(ExecedProcess *proc,
                                         const msg::ProcessInputsOutputs* const inouts) {
  TRACK(FB_DEBUG_PROC, "proc=%s", D(proc));

  /* Bubble up all the file operations we're about to perform. */
  if (proc->parent_exec_point()) {
    for (const auto& file : *inouts->inputs()->path_isreg_with_hash()) {
      Hash hash;
      assert_cmp(file->hash()->size(), ==, Hash::hash_size());
      hash.set_hash_from_binary(file->hash()->data());
      const FileUsage* fu = FileUsage::Get(ISREG_WITH_HASH, hash);
      const auto path = FileName::Get(file->path());
      proc->parent_exec_point()->propagate_file_usage(path, fu);
    }
    for (const auto& file : *inouts->inputs()->path_isdir_with_hash()) {
      Hash hash;
      assert_cmp(file->hash()->size(), ==, Hash::hash_size());
      hash.set_hash_from_binary(file->hash()->data());
      const FileUsage* fu = FileUsage::Get(ISDIR_WITH_HASH, hash);
      const auto path = FileName::Get(file->path());
      proc->parent_exec_point()->propagate_file_usage(path, fu);
    }
    for (const auto& filename : *inouts->inputs()->path_isreg()) {
      const FileUsage* fu = FileUsage::Get(ISREG);
      proc->parent_exec_point()->propagate_file_usage(FileName::Get(filename), fu);
    }
    for (const auto& filename : *inouts->inputs()->path_isdir()) {
      const FileUsage* fu = FileUsage::Get(ISDIR);
      proc->parent_exec_point()->propagate_file_usage(FileName::Get(filename), fu);
    }
    for (const auto& filename : *inouts->inputs()->path_notexist_or_isreg()) {
      const FileUsage* fu = FileUsage::Get(NOTEXIST_OR_ISREG);
      proc->parent_exec_point()->propagate_file_usage(FileName::Get(filename), fu);
    }
    for (const auto& filename : *inouts->inputs()->path_notexist_or_isreg_empty()) {
      const FileUsage* fu = FileUsage::Get(NOTEXIST_OR_ISREG_EMPTY);
      proc->parent_exec_point()->propagate_file_usage(FileName::Get(filename), fu);
    }
    for (const auto& filename : *inouts->inputs()->path_notexist()) {
      const FileUsage* fu = FileUsage::Get(NOTEXIST);
      proc->parent_exec_point()->propagate_file_usage(FileName::Get(filename), fu);
    }
  }

  /* We'll reuse this for every file modification event to propagate. */
  const FileUsage* fu = FileUsage::Get(DONTKNOW, true);

  if (!restore_dirs(proc, inouts->outputs()->path_isdir(), fu)) {
    return false;
  }

  for (const auto& file : *inouts->outputs()->path_isreg_with_hash()) {
    const auto path = FileName::Get(file->path());
    FB_DEBUG(FB_DEBUG_SHORTCUT,
             "│   Fetching file from blobs cache: "
             + d(path));
    Hash hash;
    assert_cmp(file->hash()->size(), ==, Hash::hash_size());
    hash.set_hash_from_binary(file->hash()->data());
    blob_cache->retrieve_file(hash, path);
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
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   Deleting file or directory: " + d(filename));
    if (unlink(filename->c_str()) < 0 && errno == EISDIR) {
      rmdir(filename->c_str());
    }
    if (proc->parent_exec_point()) {
      proc->parent_exec_point()->propagate_file_usage(filename, fu);
    }
  }

  /* See what the process originally wrote to its pipes. Add these to the Pipes' buffers. */
  for (const auto& data : *inouts->outputs()->pipe_data()) {
    FileFD *ffd = proc->get_fd(data->fd());
    assert(ffd);
    Pipe *pipe = ffd->pipe().get();
    assert(pipe);

    Hash hash;
    hash.set_hash_from_binary(data->hash()->data());
    int fd = blob_cache->get_fd_for_file(hash);
    struct stat64 st;
    if (fstat64(fd, &st) < 0) {
      assert(0 && "fstat");
    }
    pipe->add_data_from_fd(fd, st.st_size);

    if (proc->parent()) {
      /* Bubble up the replayed pipe data. */
      std::vector<std::shared_ptr<PipeRecorder>>& recorders =
          pipe->proc2recorders[proc->parent_exec_point()];
      PipeRecorder::record_data_from_regular_fd(&recorders, fd, st.st_size);
    }
    close(fd);
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
  TRACK(FB_DEBUG_PROC, "proc=%s", D(proc));

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
      FB_DEBUG(FB_DEBUG_SHORTCUT, "│   fingerprint = " + d(fingerprints_[proc]));
    }
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   executed path = " + d(proc->executed_path()));
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   exe = " + d(proc->executable()));
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   arg = " + d(proc->args()));
    /* FB_DEBUG(FB_DEBUG_SHORTCUT, "│   env = " + d(proc->env_vars())); */
  }

  if (proc->can_shortcut()) {
    inouts = find_shortcut(proc, &inouts_buf, &inouts_buf_len);
  }

  FB_DEBUG(FB_DEBUG_SHORTCUT, inouts ? "│ Shortcutting:" : "│ Not shortcutting.");

  if (inouts) {
    ret = apply_shortcut(proc, inouts);
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   Exiting with " + d(proc->exit_status()));
    /* Trigger cleanup of ProcessInputsOutputs. */
    inouts = nullptr;
    munmap(inouts_buf, inouts_buf_len);
  }
  FB_DEBUG(FB_DEBUG_SHORTCUT, "└─");

  proc->set_was_shortcut(ret);
  return ret;
}

}  // namespace firebuild
