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
#include "firebuild/forked_process.h"
#include "firebuild/file_name.h"
#include "firebuild/hash_cache.h"
#include "firebuild/fbbfp.h"
#include "firebuild/fbbstore.h"

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
    envs_skip_.insert(envs_skip[i].c_str());
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
  if (XXH3_128bits_update(state, hash.get_ptr(), Hash::hash_size()) == XXH_ERROR) {
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
  return Hash(digest);
}

/* Adaptor from C++ std::vector<FBBFP_Builder_file> to FBB's FBB array */
static const FBBFP_Builder *fbbfp_builder_file_vector_item_fn(int i, const void *user_data) {
  const std::vector<FBBFP_Builder_file> *fbbs =
      reinterpret_cast<const std::vector<FBBFP_Builder_file> *>(user_data);
  const FBBFP_Builder_file *builder = &(*fbbs)[i];
  return reinterpret_cast<const FBBFP_Builder *>(builder);
}

/* Adaptor from C++ std::vector<FBBFP_Builder_pipe_fds> to FBB's FBB array */
static const FBBFP_Builder *fbbfp_builder_pipe_fds_vector_item_fn(int i, const void *user_data) {
  const std::vector<FBBFP_Builder_pipe_fds> *fbbs =
      reinterpret_cast<const std::vector<FBBFP_Builder_pipe_fds> *>(user_data);
  const FBBFP_Builder_pipe_fds *builder = &(*fbbs)[i];
  return reinterpret_cast<const FBBFP_Builder *>(builder);
}

/* Adaptor from C++ std::vector<FBBSTORE_Builder_pipe_data> to FBB's FBB array */
static const FBBSTORE_Builder *fbbstore_builder_pipe_data_vector_item_fn(int i,
                                                                         const void *user_data) {
  const std::vector<FBBSTORE_Builder_pipe_data> *fbbs =
      reinterpret_cast<const std::vector<FBBSTORE_Builder_pipe_data> *>(user_data);
  const FBBSTORE_Builder_pipe_data *builder = &(*fbbs)[i];
  return reinterpret_cast<const FBBSTORE_Builder *>(builder);
}

/* Free XXH3_state_t when it is malloc()-ed. */
static inline void maybe_XXH3_freeState(XXH3_state_t* state) {
#ifndef XXH_INLINE_ALL
  XXH3_freeState(state);
#else
  (void)state;
#endif
}

/**
 * Compute the fingerprint, store it keyed by the process in fingerprints_.
 * Also store fingerprint_msgs_ if debugging is enabled.
 */
bool ExecedProcessCacher::fingerprint(const ExecedProcess *proc) {
  TRACK(FB_DEBUG_PROC, "proc=%s", D(proc));

#ifdef XXH_INLINE_ALL
  XXH3_state_t state_struct;
  XXH3_state_t* state = &state_struct;
#else
  XXH3_state_t* state = XXH3_createState();
#endif
  if (XXH3_128bits_reset_withSeed(state, kFingerprintVersion) == XXH_ERROR) {
    abort();
  }
  add_to_hash_state(state, proc->initial_wd());
  /* Size is added to not allow collisions between elements of different containers.
   * Otherwise "cmd foo BAR=1" would collide with "env BAR=1 cmd foo". */
  add_to_hash_state(state, proc->args().size());
  for (const auto& arg : proc->args()) {
    add_to_hash_state(state, arg);
  }

  /* Already sorted by the interceptor */
  add_to_hash_state(state, proc->env_vars().size());
  for (const auto& env : proc->env_vars()) {
    if (env_fingerprintable(env)) {
      add_to_hash_state(state, env);
    }
  }

  /* The executable and its hash */
  add_to_hash_state(state, proc->executable());
  Hash hash;
  if (!hash_cache->get_hash(proc->executable(), &hash)) {
    maybe_XXH3_freeState(state);
    return false;
  }
  add_to_hash_state(state, hash);

  if (proc->executable() == proc->executed_path()) {
    add_to_hash_state(state, proc->executable());
    add_to_hash_state(state, hash);
  } else if (proc->executed_path()) {
    add_to_hash_state(state, proc->executed_path());
    if (!hash_cache->get_hash(proc->executed_path(), &hash)) {
      maybe_XXH3_freeState(state);
      return false;
    }
    add_to_hash_state(state, hash);
  } else {
    add_to_hash_state(state, "");
  }

  add_to_hash_state(state, proc->libs().size());
  for (const auto lib : proc->libs()) {
    if (!hash_cache->get_hash(lib, &hash)) {
      maybe_XXH3_freeState(state);
      return false;
    }
    add_to_hash_state(state, lib);
    add_to_hash_state(state, hash);
  }

  /* The inherited outgoing pipes */
  for (const inherited_outgoing_pipe_t& inherited_outgoing_pipe :
      proc->inherited_outgoing_pipes()) {
    for (int fd : inherited_outgoing_pipe.fds) {
      add_to_hash_state(state, fd);
    }
    /* Append an invalid value to each inherited outgoing pipe to avoid collisions. */
    add_to_hash_state(state, -1);
  }

  fingerprints_[proc] = state_to_hash(state);

  if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
    /* Only when debugging: add an entry to fingerprint_msgs_.
     * The entry is the serialized message so that we don't have to fiddle with
     * memory allocation/freeing for all the substrings. */
    FBBFP_Builder_process_fingerprint fp;

    fp.set_wd(proc->initial_wd()->c_str());
    fp.set_args(proc->args());

    /* Env vars are already sorted by the interceptor, but we need to do some filtering */
    std::vector<const char *> c_env;
    c_env.reserve(proc->env_vars().size());  /* likely minor optimization */
    for (const auto& env : proc->env_vars()) {
      if (env_fingerprintable(env)) {
        c_env.push_back(env.c_str());
      }
    }
    fp.set_env_with_count(c_env.data(), c_env.size());

    /* The executable and its hash */
    FBBFP_Builder_file executable;
    if (!hash_cache->get_hash(proc->executable(), &hash)) {
      maybe_XXH3_freeState(state);
      return false;
    }
    executable.set_path(proc->executable()->c_str());
    executable.set_hash(hash.get());
    fp.set_executable(reinterpret_cast<FBBFP_Builder *>(&executable));

    FBBFP_Builder_file executed_path;
    if (proc->executable() == proc->executed_path()) {
      /* Those often match, don't create the same string twice. */
      fp.set_executed_path(reinterpret_cast<FBBFP_Builder *>(&executable));
    } else {
      if (!hash_cache->get_hash(proc->executed_path(), &hash)) {
        maybe_XXH3_freeState(state);
        return false;
      }
      executed_path.set_path(proc->executed_path()->c_str());
      executed_path.set_hash(hash.get());
      fp.set_executed_path(reinterpret_cast<FBBFP_Builder *>(&executed_path));
    }

    /* The linked libraries */
    std::vector<FBBFP_Builder_file> lib_builders;
    lib_builders.reserve(proc->libs().size());

    for (const auto& lib : proc->libs()) {
      if (!hash_cache->get_hash(lib, &hash)) {
        maybe_XXH3_freeState(state);
        return false;
      }
      FBBFP_Builder_file& lib_builder = lib_builders.emplace_back();
      lib_builder.set_path(lib->c_str());
      lib_builder.set_hash(hash.get());
    }
    fp.set_libs_item_fn(lib_builders.size(), fbbfp_builder_file_vector_item_fn, &lib_builders);

    /* The inherited pipes */
    std::vector<FBBFP_Builder_pipe_fds> pipefds_builders;
    for (const inherited_outgoing_pipe_t& inherited_outgoing_pipe :
        proc->inherited_outgoing_pipes()) {
      FBBFP_Builder_pipe_fds& pipefds_builder = pipefds_builders.emplace_back();
      pipefds_builder.set_fds(inherited_outgoing_pipe.fds);
    }
    fp.set_outgoing_pipes_item_fn(pipefds_builders.size(), fbbfp_builder_pipe_fds_vector_item_fn,
                                  &pipefds_builders);

    FBBFP_Builder *fp_generic = reinterpret_cast<FBBFP_Builder *>(&fp);
    size_t len = fp_generic->measure();
    std::vector<char> buf(len);
    fp_generic->serialize(buf.data());
    fingerprint_msgs_[proc] = buf;
  }
  maybe_XXH3_freeState(state);
  return true;
}

void ExecedProcessCacher::erase_fingerprint(const ExecedProcess *proc) {
  fingerprints_.erase(proc);
  if (FB_DEBUGGING(FB_DEBUG_CACHE) && fingerprint_msgs_.count(proc) > 0) {
    fingerprint_msgs_.erase(proc);
  }
}

static void add_file(std::vector<FBBSTORE_Builder_file>* files, const FileName* file_name,
                     const FileInfo& fi) {
  FBBSTORE_Builder_file& new_file = files->emplace_back();
  new_file.set_path_with_length(file_name->c_str(), file_name->length());
  new_file.set_type(fi.type());
  if (fi.size_known()) {
    new_file.set_size(fi.size());
  }
  if (fi.hash_known()) {
    new_file.set_hash(fi.hash().get());
  }
}

static void add_file(std::vector<FBBSTORE_Builder_file>* files, const FileName* file_name,
                     FileType type, const ssize_t content_size = -1,
                     const Hash *content_hash = nullptr, const int mode = -1) {
  FBBSTORE_Builder_file& new_file = files->emplace_back();
  new_file.set_path_with_length(file_name->c_str(), file_name->length());
  new_file.set_type(type);
  if (content_size >= 0) new_file.set_size(content_size);
  if (content_hash) new_file.set_hash(content_hash->get());
  if (mode != -1) new_file.set_mode(mode);
}

static const FBBSTORE_Builder* file_item_fn(int idx, const void *user_data) {
  const std::vector<FBBSTORE_Builder_file>* fbb_file_vector =
      reinterpret_cast<const std::vector<FBBSTORE_Builder_file> *>(user_data);
  return reinterpret_cast<const FBBSTORE_Builder *>(&(*fbb_file_vector)[idx]);
}

static bool dir_created_or_could_exist(
    const char* filename, const size_t length,
    const tsl::hopscotch_set<const FileName*>& out_path_isdir_filename_ptrs,
    const tsl::hopscotch_map<const FileName*, const FileUsage*>& file_usages) {
  const FileName* parent_dir = FileName::GetParentDir(filename, length);
  while (parent_dir != nullptr) {
    const auto it = file_usages.find(parent_dir);
    const FileUsage* fu = it->second;
    if (fu->initial_type() == NOTEXIST || fu->initial_type() == NOTEXIST_OR_ISREG
        || fu->initial_type() == NOTEXIST_OR_ISREG_EMPTY) {
      if (!fu->written()) {
        /* The process expects the directory to be missing but it does not create it.
         * This can't work. */
#ifdef FB_EXTRA_DEBUG
        assert(0 && "This should have been caught by FileUsage::merge()");
#endif
        return false;
      } else {
        if (out_path_isdir_filename_ptrs.find(parent_dir) != out_path_isdir_filename_ptrs.end()) {
          /* Directory is expected to be missing and the process creates it. Everything is OK. */
          return true;
        } else {
          FB_DEBUG(FB_DEBUG_CACHING,
                   "Regular file " + parent_dir->to_string() +
                   " is created instead of a directory");
          return false;
        }
      }
    } else if (fu->initial_type() == ISDIR) {
      /* Directory is expected to exist. */
      return true;
    }
    parent_dir = FileName::GetParentDir(parent_dir->c_str(), parent_dir->length());
  }
  return true;
}

static bool consistent_implicit_parent_dirs(
    const std::vector<FBBSTORE_Builder_file>& out_path_isreg,
    const tsl::hopscotch_set<const FileName*>& out_path_isdir_filename_ptrs,
    const tsl::hopscotch_map<const FileName*, const FileUsage*>& file_usages) {
  /* If the parent dir must not exist when shortcutting and the shortcut does not create it
   * either, then creating the new regular file would fail. */
  for (const FBBSTORE_Builder_file& file : out_path_isreg) {
    if (!dir_created_or_could_exist(file.get_path(), file.get_path_len(),
                                    out_path_isdir_filename_ptrs, file_usages)) {
      return false;
    }
  }
  /* Same for newly created dirs. */
  for (const FileName* dir : out_path_isdir_filename_ptrs) {
    if (!dir_created_or_could_exist(dir->c_str(), dir->length(),
                                    out_path_isdir_filename_ptrs, file_usages)) {
      return false;
    }
  }
  return true;
}

void ExecedProcessCacher::store(const ExecedProcess *proc) {
  TRACK(FB_DEBUG_PROC, "proc=%s", D(proc));

  if (no_store_) {
    /* This is when FIREBUILD_READONLY is set. We could have decided not to create PipeRecorders
     * at all. But maybe go with the default code path, i.e. record the data to temporary files,
     * but at the last step purge them instead of moving them to their final location in the cache.
     * This way the code path is more similar to the regular case. */
    for (const inherited_outgoing_pipe_t& inherited_outgoing_pipe :
        proc->inherited_outgoing_pipes()) {
      if (inherited_outgoing_pipe.recorder) {
        inherited_outgoing_pipe.recorder->abandon();
      }
    }
    return;
  }

  Hash fingerprint = fingerprints_[proc];

  /* Go through the files the process opened for reading and/or writing.
   * Construct the cache entry parts describing the initial and the final state
   * of them. */

  /* File inputs */
  FBBSTORE_Builder_process_inputs pi;

  std::vector<FBBSTORE_Builder_file> in_path;
  std::vector<cstring_view> in_path_notexist;

  /* File outputs */
  FBBSTORE_Builder_process_outputs po;
  std::vector<FBBSTORE_Builder_file> out_path_isreg, out_path_isdir;
  std::vector<const char *> out_path_notexist;
  /* Outputs for verification. */
  tsl::hopscotch_set<const FileName*> out_path_isdir_filename_ptrs;

  /* Construct in_path_* in 2 passes. First collect the non-system paths and then the system paths,
   * for better performance. */
  for (int i = 0; i < 2; i++) {
    for (const auto& pair : proc->file_usages()) {
      const auto filename = pair.first;
      const FileUsage* fu = pair.second;

      if (filename->is_in_system_location() == (i == 0)) {
        continue;
      }

      if (fu->generation() != filename->generation()) {
        // TODO(rbalint) extend hash cache and blob cache to reuse previously saved generations
        FB_DEBUG(FB_DEBUG_CACHING,
                 "A file (" + d(filename)+ ") changed since the process used it.");
        return;
      }

      /* If the file's initial contents matter, record it in pb's "inputs".
       * This is purely data conversion from one format to another. */
      switch (fu->initial_type()) {
        case DONTKNOW:
          /* Nothing to do. */
          break;
        case NOTEXIST:
          /* NOTEXIST is handled specially to save space in the FBB. */
          in_path_notexist.push_back({filename->c_str(), filename->length()});
          break;
        default:
          add_file(&in_path, filename, fu->initial_state());
          break;
      }
    }
  }

  for (const auto& pair : proc->file_usages()) {
    const auto filename = pair.first;
    const FileUsage* fu = pair.second;

    /* If the file's final contents matter, place it in the file cache,
     * and also record it in pb's "outputs". This actually needs to
     * compute the checksums now. */
    if (fu->written()) {
      int fd = open(filename->c_str(), O_RDONLY);
      if (fd >= 0) {
        struct stat64 st;
        if (fstat64(fd, &st) == 0) {
          if (S_ISREG(st.st_mode)) {
            Hash new_hash;
            /* TODO don't store and don't record if it was read with the same hash. */
            if (!hash_cache->store_and_get_hash(filename, &new_hash, fd, &st)) {
              /* unexpected error, now what? */
              FB_DEBUG(FB_DEBUG_CACHING,
                       "Could not store blob in cache, not writing shortcut info");
              return;
            }
            // TODO(egmont) fail if setuid/setgid/sticky is set
            int mode = st.st_mode & 07777;
            add_file(&out_path_isreg, filename, ISREG, st.st_size, &new_hash, mode);
          } else if (S_ISDIR(st.st_mode)) {
            // TODO(egmont) fail if setuid/setgid/sticky is set
            const int mode = st.st_mode & 07777;
            add_file(&out_path_isdir, filename, ISDIR, -1, nullptr, mode);
            out_path_isdir_filename_ptrs.insert(filename);
          } else {
            // TODO(egmont) handle other types of entries
          }
        } else {
          fb_perror("fstat");
          if (fu->initial_type() != NOTEXIST) {
            out_path_notexist.push_back(filename->c_str());
          }
        }
        close(fd);
      } else {
        if (fu->initial_type() != NOTEXIST) {
          out_path_notexist.push_back(filename->c_str());
        }
      }
    }
  }

  /* Pipe outputs */
  std::vector<FBBSTORE_Builder_pipe_data> out_pipe_data;

  /* Store what was written to the inherited pipes. Use the fd as of when the process started up,
   * because this is what matters if we want to replay; how the process later dup()ed it to other
   * fds is irrelevant. Similarly, no need to store the data written to pipes opened by this
   * process, that data won't ever be replayed. */
  for (const inherited_outgoing_pipe_t& inherited_outgoing_pipe :
      proc->inherited_outgoing_pipes()) {
    /* Record the output as belonging to the lowest fd. */
    int fd = inherited_outgoing_pipe.fds[0];
    std::shared_ptr<PipeRecorder> recorder = inherited_outgoing_pipe.recorder;
    if (recorder) {
      bool is_empty;
      Hash hash;
      if (!recorder->store(&is_empty, &hash)) {
        // FIXME handle error
        FB_DEBUG(FB_DEBUG_CACHING,
                 "Could not store pipe traffic in cache, not writing shortcut info");
        return;
      }
      if (!is_empty) {
        /* Note: pipes with no traffic are just simply not mentioned here in the "outputs" section.
         * They were taken into account when computing the process's fingerprint. */
        FBBSTORE_Builder_pipe_data& new_pipe_data = out_pipe_data.emplace_back();
        new_pipe_data.set_fd(fd);
        new_pipe_data.set_hash(hash.get());
      }
    }
  }

  /* Validate cache entry to be stored. */
  if (!consistent_implicit_parent_dirs(out_path_isreg,
                                       out_path_isdir_filename_ptrs,
                                       proc->file_usages())) {
    return;
  }

  /* Possibly sort the entries for easier debugging.
   *
   * Note that previously we carefully collected the non-system and system locations separately for
   * performance reasons, and now we mix the two. But again, this sorting here is for debugging. */
  if (FB_DEBUGGING(FB_DEBUG_CACHESORT)) {
    struct {
      bool operator()(const FBBSTORE_Builder_file& a, const FBBSTORE_Builder_file& b) const {
        return strcmp(a.get_path(), b.get_path()) < 0;
      }
    } file_less;
    std::sort(in_path.begin(), in_path.end(), file_less);
    std::sort(out_path_isreg.begin(), out_path_isreg.end(), file_less);
    std::sort(out_path_isdir.begin(), out_path_isdir.end(), file_less);

    struct {
      bool operator()(const cstring_view& a, const cstring_view& b) const {
        return strcmp(a.c_str, b.c_str) < 0;
      }
    } cstring_view_less;
    std::sort(in_path_notexist.begin(), in_path_notexist.end(), cstring_view_less);

    std::sort(out_path_notexist.begin(), out_path_notexist.end());
  }

  pi.set_path_item_fn(in_path.size(), file_item_fn, &in_path);
  pi.set_path_notexist(in_path_notexist);
  po.set_path_isreg_item_fn(out_path_isreg.size(), file_item_fn, &out_path_isreg);
  po.set_path_isdir_item_fn(out_path_isdir.size(), file_item_fn, &out_path_isdir);
  po.set_path_notexist(out_path_notexist);
  po.set_pipe_data_item_fn(out_pipe_data.size(), fbbstore_builder_pipe_data_vector_item_fn,
                           &out_pipe_data);
  po.set_exit_status(proc->fork_point()->exit_status());

  // TODO(egmont) Add all sorts of other stuff

  FBBSTORE_Builder_process_inputs_outputs pio;
  pio.set_inputs(reinterpret_cast<FBBSTORE_Builder *>(&pi));
  pio.set_outputs(reinterpret_cast<FBBSTORE_Builder *>(&po));

  const FBBFP_Serialized *debug_msg = NULL;
  if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
    debug_msg = reinterpret_cast<const FBBFP_Serialized *>(fingerprint_msgs_[proc].data());
  }

  /* Store in the cache everything about this process. */
  obj_cache->store(fingerprint, reinterpret_cast<FBBSTORE_Builder *>(&pio), debug_msg);
}

/**
 * Create a FileInfo object based an FBB's File entry.
 */
static FileInfo file_to_file_info(const FBBSTORE_Serialized_file *file) {
  FileInfo info(file->get_type());
  if (file->has_size()) {
    info.set_size(file->get_size());
  }
  if (file->has_hash()) {
    Hash hash(file->get_hash());
    info.set_hash(hash);
  }
  return info;
}

/**
 * Check whether the given ProcessInputs matches the file system's
 * current contents.
 */
static bool pi_matches_fs(const FBBSTORE_Serialized_process_inputs *pi, const Hash& fingerprint) {
  TRACK(FB_DEBUG_PROC, "fingerprint=%s", D(fingerprint));

  size_t i;

  for (i = 0; i < pi->get_path_count(); i++) {
    const FBBSTORE_Serialized_file *file =
        reinterpret_cast<const FBBSTORE_Serialized_file *>(pi->get_path_at(i));
    const auto path = FileName::Get(file->get_path(), file->get_path_len());
    const FileInfo query = file_to_file_info(file);
    if (!hash_cache->file_info_matches(path, query)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT, "│   " + d(fingerprint) + " mismatches e.g. at " + d(path));
      return false;
    }
  }

  for (i = 0; i < pi->get_path_notexist_count(); i++) {
    const auto path = FileName::Get(pi->get_path_notexist_at(i), pi->get_path_notexist_len_at(i));
    const FileInfo query(NOTEXIST);
    if (!hash_cache->file_info_matches(path, query)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   " + d(fingerprint)
               + " mismatches e.g. at " + d(path)
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
const FBBSTORE_Serialized_process_inputs_outputs * ExecedProcessCacher::find_shortcut(
    const ExecedProcess *proc,
    uint8_t **inouts_buf,
    size_t *inouts_buf_len) {
  TRACK(FB_DEBUG_PROC, "proc=%s", D(proc));

  const FBBSTORE_Serialized_process_inputs_outputs *inouts = nullptr;
#ifdef FB_EXTRA_DEBUG
  int count = 0;
#endif
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
    const FBBSTORE_Serialized *candidate_inouts_fbb =
        reinterpret_cast<const FBBSTORE_Serialized *>(candidate_inouts_buf);
    assert_cmp(candidate_inouts_fbb->get_tag(), ==, FBBSTORE_TAG_process_inputs_outputs);
    const FBBSTORE_Serialized_process_inputs_outputs *candidate_inouts =
        reinterpret_cast<const FBBSTORE_Serialized_process_inputs_outputs *>(candidate_inouts_fbb);

    const FBBSTORE_Serialized *inputs_fbb = candidate_inouts->get_inputs();
    assert_cmp(inputs_fbb->get_tag(), ==, FBBSTORE_TAG_process_inputs);
    const FBBSTORE_Serialized_process_inputs *inputs =
        reinterpret_cast<const FBBSTORE_Serialized_process_inputs *>(inputs_fbb);

    if (pi_matches_fs(inputs, subkey)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT, "│   " + d(subkey) + " matches the file system");
#ifdef FB_EXTRA_DEBUG
      count++;
      if (count == 1) {
#endif
        *inouts_buf = candidate_inouts_buf;
        *inouts_buf_len = candidate_inouts_buf_len;
        inouts = candidate_inouts;
#ifdef FB_EXTRA_DEBUG
        /* Let's play safe for now and not break out of this loop, let's
         * make sure that there are no other matches. */
      }
      if (count == 2) {
        FB_DEBUG(FB_DEBUG_SHORTCUT,
                 "│   More than 1 matching candidates found, still using the first one");
        munmap(candidate_inouts_buf, candidate_inouts_buf_len);
        break;
      }
#else
      /* In rare cases there could be multiple matches because the same content can be stored under
       * different file names. This is not likely to happen because if a process can be shortcut it
       * is shortcut from the existing cache object and the result is not cached again. OTOH if two
       * processes with identical inputs and outputs start and end around the same time and none of
       * them could be shortcut from the cache, then both can be cached generating differently named
       * cache files with identical content. */
      break;
#endif
    } else {
      munmap(candidate_inouts_buf, candidate_inouts_buf_len);
    }
  }
  /* The retval is currently the same as the memory address to unmap (i.e. *inouts_buf).
   * They used to be different, and could easily become different again in the future,
   * so leave the two for now. */
  return inouts;
}

/**
 * Restore output directories with the right mode.
 *
 * Create them in ascending order of the pathname lengths, so that parent directories are guaranteed
 * to be processed before its children.
 */
static bool restore_dirs(
    ExecedProcess* proc,
    const FBBSTORE_Serialized_process_outputs *outputs) {
  /* Construct indices 0 .. path_isdir_count()-1 and initialize them with these values */
  std::vector<int> indices(outputs->get_path_isdir_count());
  size_t i;
  for (i = 0; i < outputs->get_path_isdir_count(); i++) {
    indices[i] = i;
  }
  /* Sort the indices according to the pathname length at the given index */
  struct {
    const FBBSTORE_Serialized_process_outputs *outputs;
    bool operator()(const int& i1, const int& i2) const {
      const FBBSTORE_Serialized *file1_generic = outputs->get_path_isdir_at(i1);
      const FBBSTORE_Serialized_file *file1 =
          reinterpret_cast<const FBBSTORE_Serialized_file *>(file1_generic);
      const FBBSTORE_Serialized *file2_generic = outputs->get_path_isdir_at(i2);
      const FBBSTORE_Serialized_file *file2 =
          reinterpret_cast<const FBBSTORE_Serialized_file *>(file2_generic);
      return file1->get_path_len() < file2->get_path_len();
    }
  } pathname_length_less;
  pathname_length_less.outputs = outputs;
  std::sort(indices.begin(), indices.end(), pathname_length_less);
  /* Process the directory names in ascending order of their lengths */
  for (i = 0; i < outputs->get_path_isdir_count(); i++) {
    const FBBSTORE_Serialized *dir_generic = outputs->get_path_isdir_at(indices[i]);
    assert_cmp(dir_generic->get_tag(), ==, FBBSTORE_TAG_file);
    const FBBSTORE_Serialized_file *dir =
        reinterpret_cast<const FBBSTORE_Serialized_file *>(dir_generic);
    const auto path = FileName::Get(dir->get_path(), dir->get_path_len());
    assert(dir->has_mode());
    mode_t mode = dir->get_mode();
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   Creating directory: " + d(path));
    int ret = mkdir(path->c_str(), mode);
    if (ret != 0) {
      if (errno == EEXIST) {
        struct stat64 st;
        if (stat64(path->c_str(), &st) != 0) {
          fb_perror("Failed to stat() existing pathname");
          assert_cmp(ret, !=, -1);
          return false;
        }
        if (!S_ISDIR(st.st_mode)) {
          fb_perror("Failed to restore directory, the target already exists and is not a dir");
          assert_cmp(ret, !=, -1);
          return false;
        }
        if (chmod(path->c_str(), mode) != 0) {
          fb_perror("Failed to restore directory's permissions");
          assert_cmp(ret, !=, -1);
          return false;
        }
      } else {
        fb_perror("Failed to restore directory");
        assert_cmp(ret, !=, -1);
        return false;
      }
    }
    if (proc->parent_exec_point()) {
      proc->parent_exec_point()->register_file_usage_update(
          path, FileUsageUpdate(path, DONTKNOW, true));
    }
  }
  return true;
}

/**
 * Remove files and directories.
 *
 * Remove them in descending order of the pathname lengths, so that parent directories are
 * guaranteed to be processed after its children.
 *
 * Note: There's deliberately no error checking. Maybe a program creates a temporary file in a way
 * that we cannot tell whether that file existed previously, and then deletes it. So by design the
 * unlink attempt might fail. Maybe one day we can refine this logic.
 */
static void remove_files_and_dirs(
    ExecedProcess* proc,
    const FBBSTORE_Serialized_process_outputs *outputs) {
  /* Construct indices 0 .. path_notexist_count()-1 and initialize them with these values */
  std::vector<int> indices(outputs->get_path_notexist_count());
  size_t i;
  for (i = 0; i < outputs->get_path_notexist_count(); i++) {
    indices[i] = i;
  }
  /* Reverse sort the indices according to the pathname length at the given index */
  struct {
    const FBBSTORE_Serialized_process_outputs *outputs;
    bool operator()(const int& i1, const int& i2) const {
      int len1 = outputs->get_path_notexist_len_at(i1);
      int len2 = outputs->get_path_notexist_len_at(i2);
      return len1 > len2;
    }
  } pathname_length_greater;
  pathname_length_greater.outputs = outputs;
  std::sort(indices.begin(), indices.end(), pathname_length_greater);
  /* Process the directory names in descending order of their lengths */
  for (i = 0; i < outputs->get_path_notexist_count(); i++) {
    const auto path = FileName::Get(outputs->get_path_notexist_at(indices[i]),
                                    outputs->get_path_notexist_len_at(indices[i]));
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   Deleting file or directory: " + d(path));
    if (unlink(path->c_str()) < 0 && errno == EISDIR) {
      rmdir(path->c_str());
    }
    if (proc->parent_exec_point()) {
      proc->parent_exec_point()->register_file_usage_update(
          path, FileUsageUpdate(path, DONTKNOW, true));
    }
  }
}

/**
 * Applies the given shortcut.
 *
 * Modifies the file system to match the given instructions. Propagates
 * upwards all the shortcutted file read and write events.
 */
bool ExecedProcessCacher::apply_shortcut(ExecedProcess *proc,
                                         const FBBSTORE_Serialized_process_inputs_outputs *inouts) {
  TRACK(FB_DEBUG_PROC, "proc=%s", D(proc));

  size_t i;

  /* Bubble up all the file operations we're about to perform. */
  if (proc->parent_exec_point()) {
    const FBBSTORE_Serialized_process_inputs *inputs =
        reinterpret_cast<const FBBSTORE_Serialized_process_inputs *>
        (inouts->get_inputs());

    for (i = 0; i < inputs->get_path_count(); i++) {
      const FBBSTORE_Serialized_file *file =
          reinterpret_cast<const FBBSTORE_Serialized_file *>(inputs->get_path_at(i));
      const auto path = FileName::Get(file->get_path(), file->get_path_len());
      FileInfo info = file_to_file_info(file);
      proc->parent_exec_point()->register_file_usage_update(path, FileUsageUpdate(path, info));
    }
    for (i = 0; i < inputs->get_path_notexist_count(); i++) {
      const auto path = FileName::Get(inputs->get_path_notexist_at(i),
                                      inputs->get_path_notexist_len_at(i));
      proc->parent_exec_point()->register_file_usage_update(path, FileUsageUpdate(path, NOTEXIST));
    }
  }

  const FBBSTORE_Serialized_process_outputs *outputs =
      reinterpret_cast<const FBBSTORE_Serialized_process_outputs *>
      (inouts->get_outputs());

  if (!restore_dirs(proc, outputs)) {
    return false;
  }

  for (i = 0; i < outputs->get_path_isreg_count(); i++) {
    const FBBSTORE_Serialized_file *file =
        reinterpret_cast<const FBBSTORE_Serialized_file *>(outputs->get_path_isreg_at(i));
    const auto path = FileName::Get(file->get_path(), file->get_path_len());
    FB_DEBUG(FB_DEBUG_SHORTCUT,
             "│   Fetching file from blobs cache: "
             + d(path));
    Hash hash(file->get_hash());
    blob_cache->retrieve_file(hash, path);
    if (file->has_mode()) {
      /* Refuse to apply setuid, setgid, sticky bit. */
      // FIXME warn on them, even when we store them.
      chmod(path->c_str(), file->get_mode() & 0777);
    }
    if (proc->parent_exec_point()) {
      proc->parent_exec_point()->register_file_usage_update(
          path, FileUsageUpdate(path, DONTKNOW, true));
    }
  }

  remove_files_and_dirs(proc, outputs);

  /* See what the process originally wrote to its pipes. Add these to the Pipes' buffers. */
  for (i = 0; i < outputs->get_pipe_data_count(); i++) {
    const FBBSTORE_Serialized_pipe_data *data =
        reinterpret_cast<const FBBSTORE_Serialized_pipe_data *>
        (outputs->get_pipe_data_at(i));
    FileFD *ffd = proc->get_fd(data->get_fd());
    assert(ffd);
    Pipe *pipe = ffd->pipe().get();
    assert(pipe);

    Hash hash(data->get_hash());
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
  proc->fork_point()->set_exit_status(outputs->get_exit_status());

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
  const FBBSTORE_Serialized_process_inputs_outputs *inouts = NULL;

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
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   Exiting with " + d(proc->fork_point()->exit_status()));
    /* Trigger cleanup of ProcessInputsOutputs. */
    inouts = nullptr;
    munmap(inouts_buf, inouts_buf_len);
  }
  FB_DEBUG(FB_DEBUG_SHORTCUT, "└─");

  proc->set_was_shortcut(ret);
  return ret;
}

}  /* namespace firebuild */
