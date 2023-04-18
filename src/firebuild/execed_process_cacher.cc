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

#include "firebuild/execed_process_cacher.h"
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "firebuild/config.h"
#include "firebuild/debug.h"
#include "firebuild/execed_process.h"
#include "firebuild/forked_process.h"
#include "firebuild/file_name.h"
#include "firebuild/hash_cache.h"
#include "firebuild/fbbfp.h"
#include "firebuild/fbbstore.h"
#include "firebuild/process_tree.h"

extern bool generate_report;

namespace firebuild {

static const XXH64_hash_t kFingerprintVersion = 0;
static const unsigned int kCacheFormatVersion = 1;
static const char kCacheStatsFile[] = "stats";
static const char kCacheSizeFile[] = "size";

unsigned int ExecedProcessCacher::cache_format_ = 0;

/* singleton*/
ExecedProcessCacher* execed_process_cacher;

void ExecedProcessCacher::init(const libconfig::Config* cfg) {
  std::string cache_dir;
  char* cache_dir_env;
  if ((cache_dir_env = getenv("FIREBUILD_CACHE_DIR")) && cache_dir_env[0] != '\0') {
    cache_dir = std::string(cache_dir_env);
  } else if ((cache_dir_env = getenv("XDG_CACHE_HOME")) && cache_dir_env[0] != '\0') {
    cache_dir = std::string(cache_dir_env) + "/firebuild";
  } else if ((cache_dir_env = getenv("HOME")) && cache_dir_env[0] != '\0') {
    cache_dir = std::string(cache_dir_env) + "/.cache/firebuild";
  } else {
    fb_error("Please set HOME or XDG_CACHE_HOME or FIREBUILD_CACHE_DIR to let "
             "firebuild place the cache somewhere.");
    exit(EXIT_FAILURE);
  }

  /* Like CCACHE_RECACHE: Don't fetch entries from the cache, but still
   * potentially store new ones. Note however that it might decrease the
   * objcache hit ratio: new entries might be stored that eventually
   * result in the same operation, but go through a slightly different
   * path (e.g. different tmp file name), and thus look different in
   * Firebuild's eyes. Firebuild refuses to shortcut a process if two or
   * more matches are found in the objcache. */
  bool no_fetch {getenv("FIREBUILD_RECACHE") != NULL};
  /* Like CCACHE_READONLY: Don't store new results in the cache. */
  bool no_store = getenv("FIREBUILD_READONLY") != NULL;

  struct stat st;
  if (stat(cache_dir.c_str(), &st) == 0) {
    if (!S_ISDIR(st.st_mode)) {
      fb_error("cache dir exists but is not a directory");
      exit(EXIT_FAILURE);
    }
  } else {
    if (mkdirhier(cache_dir.c_str(), 0700) != 0) {
      fb_perror("mkdir");
      exit(EXIT_FAILURE);
    }
  }
  char* cache_format_file = strdup((cache_dir + "/cache-format").c_str());
  if (stat(cache_format_file, &st) == 0) {
    if (!S_ISREG(st.st_mode)) {
      fb_error("$FIREBUILD_CACHE_DIR/cache-format exists but is not a regular file");
      exit(EXIT_FAILURE);
    }
    FILE* f;
    if (!(f = fopen(cache_format_file, "r"))) {
      fb_perror("opening cache-format file failed");
      exit(EXIT_FAILURE);
    } else {
      if (fscanf(f, "%u\n", &cache_format_) != 1 || cache_format() > kCacheFormatVersion) {
        fb_error("Cache format version is not supported, not reading or writing the cache");
        no_fetch = true;
        no_store = true;
      } else if (cache_format() == kCacheFormatVersion) {
        /* Current format, we can use the cache. */
      } else {
        /* Cache is in a prior format. Either use it considering the differences where needed
         * or upgrade it. */
      }
      fclose(f);
    }
  } else {
    FILE* f;
    if (!(f = fopen(cache_format_file, "wx"))) {
      fb_perror("creating cache-format file failed");
      exit(EXIT_FAILURE);
    }
    if (fprintf(f, "%d\n", kCacheFormatVersion) <= 0) {
      fb_perror("writing cache-format file failed");
      exit(EXIT_FAILURE);
    }
    fclose(f);
  }
  free(cache_format_file);

  blob_cache = new BlobCache(cache_dir + "/blobs");
  obj_cache = new ObjCache(cache_dir + "/objs");
  PipeRecorder::set_base_dir((cache_dir + "/tmp").c_str());
  hash_cache = new HashCache();

  execed_process_cacher = new ExecedProcessCacher(no_store, no_fetch, cache_dir, cfg);
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
 * Add string to fingerprint, including the ending '\0'.
 *
 * Adding the ending '\0' prevents hash collisions by concatenating strings.
 */
static void add_to_hash_state(XXH3_state_t* state, const char* str, size_t length) {
  if (XXH3_128bits_update(state, str, length + 1) == XXH_ERROR) {
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

/* Free XXH3_state_t when it is malloc()-ed. */
static inline void maybe_XXH3_freeState(XXH3_state_t* state) {
#ifndef XXH_INLINE_ALL
  XXH3_freeState(state);
#else
  (void)state;
#endif
}

ExecedProcessCacher::ExecedProcessCacher(bool no_store,
                                         bool no_fetch,
                                         const std::string& cache_dir,
                                         const libconfig::Config* cfg) :
    no_store_(no_store), no_fetch_(no_fetch),
    envs_skip_(), ignore_locations_hash_(), fingerprints_(), fingerprint_msgs_(),
    cache_dir_(cache_dir) {
  try {
    const libconfig::Setting& envs_skip = cfg->getRoot()["env_vars"]["fingerprint_skip"];
    for (int i = 0; i < envs_skip.getLength(); i++) {
      envs_skip_.insert(envs_skip[i].c_str());
    }
  } catch(libconfig::SettingNotFoundException&) {
    /* Configuration setting may be missing. This is OK. */
  }
#ifdef XXH_INLINE_ALL
  XXH3_state_t state_struct;
  XXH3_state_t* state = &state_struct;
#else
  XXH3_state_t* state = XXH3_createState();
#endif
  if (XXH3_128bits_reset(state) == XXH_ERROR) {
    abort();
  }
  /* Hash the already sorted ignore locations.*/
  for (int i = 0; i < ignore_locations.len; i++) {
    add_to_hash_state(state, ignore_locations.p[i].c_str, ignore_locations.p[i].length);
  }
  ignore_locations_hash_ = state_to_hash(state);
  maybe_XXH3_freeState(state);
}

bool ExecedProcessCacher::env_fingerprintable(const std::string& name_and_value) const {
  /* Strip off the "=value" part. */
  const std::string name = name_and_value.substr(0, name_and_value.find('='));

  /* Env vars to skip, taken from the config files.
   * Note: FB_SOCKET is already filtered out in the interceptor. */
  return envs_skip_.find(name) == envs_skip_.end();
}

/* Adaptor from C++ std::vector<FBBFP_Builder_file> to FBB's FBB array */
static const FBBFP_Builder *fbbfp_builder_file_vector_item_fn(int i, const void *user_data) {
  auto fbbs = reinterpret_cast<const std::vector<FBBFP_Builder_file> *>(user_data);
  const FBBFP_Builder_file *builder = &(*fbbs)[i];
  return reinterpret_cast<const FBBFP_Builder *>(builder);
}

/* Adaptor from C++ std::vector<FBBFP_Builder_ofd> to FBB's FBB array */
static const FBBFP_Builder *fbbfp_builder_ofd_vector_item_fn(int i, const void *user_data) {
  auto fbbs = reinterpret_cast<const std::vector<FBBFP_Builder_ofd> *>(user_data);
  const FBBFP_Builder_ofd *builder = &(*fbbs)[i];
  return reinterpret_cast<const FBBFP_Builder *>(builder);
}

/* Adaptor from C++ std::vector<FBBSTORE_Builder_append_to_fd> to FBB's FBB array */
static const FBBSTORE_Builder *fbbstore_builder_append_to_fd_vector_item_fn(int i,
                                                                            const void *user_data) {
  auto fbbs = reinterpret_cast<const std::vector<FBBSTORE_Builder_append_to_fd> *>(user_data);
  const FBBSTORE_Builder_append_to_fd *builder = &(*fbbs)[i];
  return reinterpret_cast<const FBBSTORE_Builder *>(builder);
}

static void hash_param_file(XXH3_state_t* state, const ExecedProcess *proc,
                            const std::string& file, Hash* hash) {
  bool is_dir;
  if (hash_cache->get_hash(proc->get_absolute(AT_FDCWD, file.c_str(), file.length()),
                           0, hash, &is_dir)) {
    if (is_dir) {
      /* Directory params are not hashed. */
      *hash = Hash();
    }
  } else {
    /* File may be an output file or not a file at all */
    *hash = Hash();
  }
  add_to_hash_state(state, *hash);
}

/*
 * Note: Don't forget updating the debugging part below, too, when changing the
 * fingerprint generation!
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
  add_to_hash_state(state, ignore_locations_hash_);
  add_to_hash_state(state, proc->initial_wd());
  /* Size is added to not allow collisions between elements of different containers.
   * Otherwise "cmd foo BAR=1" would collide with "env BAR=1 cmd foo". */
  add_to_hash_state(state, proc->args().size());
  const std::vector<std::string>& args = proc->args();
  const bool guess_file_params = quirks & FB_QUIRK_GUESS_FILE_PARAMS;
  std::string found_param_file;
  Hash found_param_file_hash;
  for (const auto& arg : args) {
    add_to_hash_state(state, arg);
    /* Since we are already iterating over the args let's find a hint for hash_param_files(). */
    if (guess_file_params && (arg == "conftest.c" || arg == "objs/autotest.c")) {
      found_param_file = arg;
    }
  }

  /* Heuristics for including some parameter files in the fingerprint.
   * For now only a single, already found parameter is covered.
   * Note: Update kFingerprintVersion whenever this heuristic changes. */
  if (guess_file_params && found_param_file.size() > 0) {
    /* Number of files to be added. */
    add_to_hash_state(state, 1);
    hash_param_file(state, proc, found_param_file, &found_param_file_hash);
  } else {
    add_to_hash_state(state, 0);
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
  if (!hash_cache->get_hash(proc->executable(), 0, &hash)) {
    FB_DEBUG(FB_DEBUG_PROC, "Could not get hash of executable: " + d(proc->executable()));
    maybe_XXH3_freeState(state);
    return false;
  }
  add_to_hash_state(state, hash);

  if (proc->executable() == proc->executed_path()) {
    /* Those often match. Don't calculate the same hash twice then. */
    add_to_hash_state(state, proc->executable());
    add_to_hash_state(state, hash);
  } else {
    add_to_hash_state(state, proc->executed_path());
    if (!hash_cache->get_hash(proc->executed_path(), 0, &hash)) {
      FB_DEBUG(FB_DEBUG_PROC, "Could not get hash of executed path: " + d(proc->executed_path()));
      maybe_XXH3_freeState(state);
      return false;
    }
    add_to_hash_state(state, hash);
  }

  add_to_hash_state(state, proc->original_executed_path());

  add_to_hash_state(state, proc->libs().size());
  for (const auto lib : proc->libs()) {
#ifdef __APPLE__
    /* SDK libraries are not present as files, see:
     * https://developer.apple.com/forums/thread/655588 */
    if (strncmp(lib->c_str(), "/usr/lib/", strlen("/usr/lib/")) == 0) {
      continue;
    }
#endif
    if (!hash_cache->get_hash(lib, 0, &hash)) {
      FB_DEBUG(FB_DEBUG_PROC, "Could not get hash of library: " + d(lib));
      maybe_XXH3_freeState(state);
      return false;
    }
    add_to_hash_state(state, lib);
    add_to_hash_state(state, hash);
  }

  /* umask */
  add_to_hash_state(state, proc->umask());

  /* The inherited files */
  for (const inherited_file_t& inherited_file : proc->inherited_files()) {
    /* Workaround for #938. */
    fd_type pretended_type = inherited_file.type == FD_PIPE_IN ? FD_IGNORED : inherited_file.type;
    add_to_hash_state(state, pretended_type);
    for (int fd : inherited_file.fds) {
      add_to_hash_state(state, fd);
    }
    /* Append an invalid value to each inherited file to avoid collisions. */
    add_to_hash_state(state, -1);
  }

  fingerprints_[proc] = state_to_hash(state);

  if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
    /* Only when debugging: add an entry to fingerprint_msgs_.
     * The entry is the serialized message so that we don't have to fiddle with
     * memory allocation/freeing for all the substrings. */
    FBBFP_Builder_process_fingerprint fp;

    fp.set_kFingerprintVersion(kFingerprintVersion);
    std::vector<std::string> ignore_locations_vec;
    for (int i = 0; i < ignore_locations.len; i++) {
      ignore_locations_vec.push_back(ignore_locations.p[i].c_str);
    }
    fp.set_ignore_locations(ignore_locations_vec);

    fp.set_wd(proc->initial_wd()->c_str());
    fp.set_args(proc->args());

    if (guess_file_params && found_param_file.size() > 0) {
      fp.set_param_file_hash(found_param_file_hash.get());
    }

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
    if (!hash_cache->get_hash(proc->executable(), 0, &hash)) {
      FB_DEBUG(FB_DEBUG_PROC, "Could not get hash of executable: " + d(proc->executable()));
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
      if (!hash_cache->get_hash(proc->executed_path(), 0, &hash)) {
        FB_DEBUG(FB_DEBUG_PROC, "Could not get hash of executed path: " + d(proc->executed_path()));
        maybe_XXH3_freeState(state);
        return false;
      }
      executed_path.set_path(proc->executed_path()->c_str());
      executed_path.set_hash(hash.get());
      fp.set_executed_path(reinterpret_cast<FBBFP_Builder *>(&executed_path));
    }

    fp.set_original_executed_path(proc->original_executed_path());

    /* The linked libraries */
    std::vector<FBBFP_Builder_file> lib_builders;
    lib_builders.reserve(proc->libs().size());

    for (const auto& lib : proc->libs()) {
#ifdef __APPLE__
      /* SDK libraries are not present as files, see:
       * https://developer.apple.com/forums/thread/655588 */
      if (strncmp(lib->c_str(), "/usr/lib/", strlen("/usr/lib/")) == 0) {
        continue;
      }
#endif
      if (!hash_cache->get_hash(lib, 0, &hash)) {
        FB_DEBUG(FB_DEBUG_PROC, "Could not get hash of library: " + d(lib));
        maybe_XXH3_freeState(state);
        return false;
      }
      FBBFP_Builder_file& lib_builder = lib_builders.emplace_back();
      lib_builder.set_path(lib->c_str());
      lib_builder.set_hash(hash.get());
    }
    fp.set_libs_item_fn(lib_builders.size(), fbbfp_builder_file_vector_item_fn, &lib_builders);

    /* umask */
    fp.set_umask(proc->umask());

    /* The inherited files */
    std::vector<FBBFP_Builder_ofd> ofd_builders;
    for (const inherited_file_t& inherited_file : proc->inherited_files()) {
      FBBFP_Builder_ofd& ofd_builder = ofd_builders.emplace_back();
      /* Workaround for #938. */
      fd_type pretended_type = inherited_file.type == FD_PIPE_IN ? FD_IGNORED : inherited_file.type;
      ofd_builder.set_type(pretended_type);
      ofd_builder.set_fds(inherited_file.fds);
    }
    fp.set_ofds_item_fn(ofd_builders.size(), fbbfp_builder_ofd_vector_item_fn, &ofd_builders);

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
  if (fi.mode_mask() != 0) {
    new_file.set_mode(fi.mode());
    new_file.set_mode_mask(fi.mode_mask());
  }
}

static const FBBSTORE_Builder* file_item_fn(int idx, const void *user_data) {
  auto fbb_file_vector = reinterpret_cast<const std::vector<FBBSTORE_Builder_file> *>(user_data);
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
    if (fu->initial_type() == NOTEXIST || fu->initial_type() == NOTEXIST_OR_ISREG) {
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

static bool tmp_file_or_on_tmp_path(const FileUsage* fu, const FileName* filename,
                                    const FileName* tmpdir) {
  if (fu->tmp_file()) {
    return true;
  } else {
    if (strncmp(filename->c_str(), tmpdir->c_str(), tmpdir->length()) == 0
        && filename->c_str()[tmpdir->length()] == '/') {
      const FileName* top_dir = proc_tree->top_dir();
      assert(top_dir);
      return !(strncmp(filename->c_str(), top_dir->c_str(), top_dir->length()) == 0
               && filename->c_str()[top_dir->length()] == '/');
    } else {
      return false;
    }
  }
}

static bool rustc_deps_dir(const ExecedProcess* const proc, const FileName* const filename) {
  const std::vector<std::string> &args = proc->args();
  if (args[0] == "rustc") {
    for (const std::string& arg : args) {
      if (arg.starts_with("dependency=")) {
        const std::string dependency_dir(arg.substr(strlen("dependency=")));
        /* Assumes that the dependency dir is already absolute. */
        if (dependency_dir == filename->to_string()) {
          return true;
        }
      }
    }
  }
  return false;
}


void ExecedProcessCacher::store(ExecedProcess *proc) {
  TRACK(FB_DEBUG_PROC, "proc=%s", D(proc));

  if (no_store_) {
    /* This is when FIREBUILD_READONLY is set. We could have decided not to create PipeRecorders
     * at all. But maybe go with the default code path, i.e. record the data to temporary files,
     * but at the last step purge them instead of moving them to their final location in the cache.
     * This way the code path is more similar to the regular case. */
    for (const inherited_file_t& inherited_file : proc->inherited_files()) {
      if (inherited_file.recorder) {
        assert(inherited_file.type == FD_PIPE_OUT);
        inherited_file.recorder->abandon();
      }
    }
    return;
  }

  bool parent_may_be_just_sh_c_this = false;
  const ExecedProcess* const parent_exec_point = proc->parent_exec_point();
  if (parent_exec_point) {
    if (ccache_disabled
        && parent_exec_point->executable()->without_dirs() == "ccache"
        && parent_exec_point->can_shortcut()) {
      proc->disable_shortcutting_only_this("Shortcut parent ccache ... instead");
      return;
    }

    // Parent may just be sh -c <this command>, detect that
    parent_may_be_just_sh_c_this = (parent_exec_point->can_shortcut()
                                    && parent_exec_point->args().size() == 3
                                    && parent_exec_point->args()[1] == "-c"
                                    && shells->contains(parent_exec_point->args()[0]));
  }

  // TODO(rbalint) narrow down the cases when all args are checked
  const std::vector<std::string>& args = proc->args();
  std::string joined_cmdline = "";
  for (const auto& arg : args) {
    if (parent_may_be_just_sh_c_this) {
      joined_cmdline += (joined_cmdline == "") ? arg : (" " + arg);
    }
    if (arg == "-emit-pch") {
      bool fno_pch_timestamp_found = false;
      for (const auto& arg_inner_loop : args) {
        if (arg_inner_loop == "-fno-pch-timestamp") {
          fno_pch_timestamp_found = true;
          break;
        }
      }
      if (!fno_pch_timestamp_found) {
        proc->disable_shortcutting_bubble_up(
            "Clang's -emit-pch without -Xclang -fno-pch-timestamp prevents shortcutting");
        return;
      }
      break;
    }
  }

  if (parent_may_be_just_sh_c_this && joined_cmdline == parent_exec_point->args()[2]) {
    proc->disable_shortcutting_only_this("Shortcut parent sh -c ... instead");
    return;
  }

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
  const FileName* const tmpdir = FileName::default_tmpdir;
  size_t in_path_non_system_count {0},
      in_path_notexist_non_system_count {0};

  /* Construct in_path_* in 2 passes. First collect the non-system paths and then the system paths,
   * for better performance. */
  for (int i = 0; i < 2; i++) {
    for (const auto& pair : proc->file_usages()) {
      const auto filename = pair.first;
      const FileUsage* fu = pair.second;

      if (filename->is_in_read_only_location() == (i == 0)) {
        continue;
      }

      if (fu->generation() != filename->generation()) {
        // TODO(rbalint) extend hash cache and blob cache to reuse previously saved generations
        FB_DEBUG(FB_DEBUG_CACHING,
                 "A file (" + d(filename)+ ") changed since the process used it.");
        proc->disable_shortcutting_only_this(
            generate_report
            ? deduplicated_string("A file (" + d(filename)
                                  + ") changed since the process used it.").c_str()
            : "A file could not be stored because it changed since the process used it.");
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
        case ISDIR:
          if (fu->initial_state().hash_known()
              && ((quirks & FB_QUIRK_IGNORE_TMP_LISTING && filename == tmpdir)
                  || rustc_deps_dir(proc, filename))) {
            FileInfo no_hash_initial_state(fu->initial_state());
            no_hash_initial_state.set_hash(nullptr);
            add_file(&in_path, filename, no_hash_initial_state);
            break;
          }
          [[fallthrough]];
        default:
          if (fu->initial_state().type() == ISREG
              && tmp_file_or_on_tmp_path(fu, filename, tmpdir)) {
            FB_DEBUG(FB_DEBUG_CACHING, "Not storing cache entry because it read "
                     + d(filename) + ", which is a temporary file");
            return;
          }
          add_file(&in_path, filename, fu->initial_state());
          break;
      }
    }
    in_path_non_system_count = in_path.size();
    in_path_notexist_non_system_count = in_path_notexist.size();
  }

  uint64_t stored_blob_bytes = 0;
  for (const auto& pair : proc->file_usages()) {
    const auto filename = pair.first;
    const FileUsage* fu = pair.second;
    /* fu contains information about the file's original contents or metadata, plus whether it's
     * been modified. We need to query the current state of the file if anything has been modified. */

    if (!fu->written() && !fu->mode_changed()) {
      /* Completely unchanged, nothing to do. */
      continue;
    }

    FileInfo new_file_info(DONTKNOW);

    struct stat64 st;
    if (stat64(filename->c_str(), &st) == 0) {
      /* We have something, let's see what it is. */
      new_file_info.set_type(EXIST);

      /* If the file's final contents matter, place it in the file cache,
       * and also record it in pb's "outputs". This actually needs to
       * compute the checksums now. */
      if (fu->written()) {
        if (S_ISREG(st.st_mode)) {
          new_file_info.set_type(ISREG);
          Hash new_hash;
          /* TODO don't store and don't record if it was read with the same hash. */
          int fd = open(filename->c_str(), O_RDONLY);
          if (fd >= 0) {
            off_t stored_bytes = 0;
            if (!hash_cache->store_and_get_hash(filename, 0, &new_hash, &stored_bytes, fd, &st)) {
              /* unexpected error, now what? */
              FB_DEBUG(FB_DEBUG_CACHING,
                       "Could not store blob in cache, not writing shortcut info");
              close(fd);
              proc->disable_shortcutting_only_this(
                  "Could not store blob in cache, not writing shortcut info");
              return;
            }
            close(fd);
            new_file_info.set_size(st.st_size);
            new_file_info.set_hash(new_hash);
            if ((stored_blob_bytes += stored_bytes) > max_entry_size) {
              FB_DEBUG(FB_DEBUG_CACHING,
                       "Could not store blob in cache because it would exceed max_entry_size");
              return;
            }
          } else {
            fb_perror("open");
            new_file_info.set_type(NOTEXIST);
          }
        } else if (S_ISDIR(st.st_mode)) {
          new_file_info.set_type(ISDIR);
        } else {
          // TODO(egmont) handle other types of entries
          new_file_info.set_type(NOTEXIST);
        }
      }

      if (fu->mode_changed()) {
        // TODO(egmont) fail if setuid/setgid/sticky is set
        new_file_info.set_mode_bits(st.st_mode & 07777, 07777);
      }
    } else {
      /* Stat failed, nothing at the new location. */
      new_file_info.set_type(NOTEXIST);
    }

    switch (new_file_info.type()) {
      case DONTKNOW:
        /* This can happen if we figured out that the file didn't actually change. */
        break;
      case EXIST:
      case ISREG:
        // FIXME skip adding if the new state is the same as the old one
        if (tmp_file_or_on_tmp_path(fu, filename, tmpdir)) {
          FB_DEBUG(FB_DEBUG_CACHING,
                   "Temporary file (" + d(filename) + ") can't be process output.");
          proc->disable_shortcutting_only_this("Process created a temporary file");
          return;
        }
        add_file(&out_path_isreg, filename, new_file_info);
        break;
      case ISDIR:
        // FIXME skip adding if the new state is the same as the old one
        if (tmp_file_or_on_tmp_path(fu, filename, tmpdir)) {
          FB_DEBUG(FB_DEBUG_CACHING,
                   "Temporary dir (" + d(filename) + ") can't be process output.");
          proc->disable_shortcutting_only_this("Process created a temporary dir");
          return;
        }
        add_file(&out_path_isdir, filename, new_file_info);
        out_path_isdir_filename_ptrs.insert(filename);
        break;
      case NOTEXIST:
        if (fu->initial_type() != NOTEXIST) {
          out_path_notexist.push_back(filename->c_str());
        }
        break;
      default:
        assert(0);
    }
  }

  /* Data appended to inherited files (pipes, regular files) */
  std::vector<FBBSTORE_Builder_append_to_fd> out_append_to_fd;

  /* Store what was written to the inherited pipes. Use the fd as of when the process started up,
   * because this is what matters if we want to replay; how the process later dup()ed it to other
   * fds is irrelevant. Similarly, no need to store the data written to pipes opened by this
   * process, that data won't ever be replayed. */
  for (const inherited_file_t& inherited_file : proc->inherited_files()) {
    if (is_write(inherited_file.flags)) {
      /* Record the output as belonging to the lowest fd. */
      int fd = inherited_file.fds[0];

      if (inherited_file.type == FD_PIPE_OUT) {
        std::shared_ptr<PipeRecorder> recorder = inherited_file.recorder;
        if (recorder) {
          bool is_empty;
          Hash hash;
          off_t stored_bytes = 0;
          if (!recorder->store(&is_empty, &hash, &stored_bytes)) {
            // FIXME handle error
            FB_DEBUG(FB_DEBUG_CACHING,
                     "Could not store pipe traffic in cache, not writing shortcut info");
            proc->disable_shortcutting_only_this(
                "Could not store pipe traffic in cache, not writing shortcut info");
            return;
          }
          if ((stored_blob_bytes += stored_bytes) > max_entry_size) {
            FB_DEBUG(FB_DEBUG_CACHING,
                     "Could not store blob in cache because it would exceed max_entry_size");
            return;
          }

          if (!is_empty) {
            /* Note: pipes with no traffic are just simply not mentioned here in the "outputs" section.
             * They were taken into account when computing the process's fingerprint. */
            FBBSTORE_Builder_append_to_fd& new_append = out_append_to_fd.emplace_back();
            new_append.set_fd(fd);
            new_append.set_hash(hash.get());
          }
        }
      } else if (inherited_file.type == FD_FILE) {
        Hash hash;
        struct stat64 st;
        if (stat64(inherited_file.filename->c_str(), &st) < 0) {
          // FIXME handle error
          FB_DEBUG(FB_DEBUG_CACHING,
                   "Could not stat file, not writing shortcut info");
          proc->disable_shortcutting_only_this(
              "Could not stat file, not writing shortcut info");
          return;
        } else if (!S_ISREG(st.st_mode)) {
          // FIXME handle error
          FB_DEBUG(FB_DEBUG_CACHING,
                     "Not a regular file, not writing shortcut info");
          proc->disable_shortcutting_only_this(
              "Not a regular file, not writing shortcut info");
          return;
        } else if (st.st_size < inherited_file.start_offset) {
          // FIXME handle error
          FB_DEBUG(FB_DEBUG_CACHING,
                     "File shrank during appending, not writing shortcut info");
          proc->disable_shortcutting_only_this(
              "File shrank during appending, not writing shortcut info");
          return;
        } else if (st.st_size > inherited_file.start_offset) {
          /* Note: files that weren't appended to are just simply not mentioned here in the
           * "outputs" section. They were taken into account when computing the fingerprint. */
          if (!blob_cache->store_file(inherited_file.filename, 1, -1, inherited_file.start_offset,
                                      st.st_size, &hash)) {
            // FIXME handle error
            FB_DEBUG(FB_DEBUG_CACHING,
                     "Could not store file fragment in cache, not writing shortcut info");
            proc->disable_shortcutting_only_this(
                "Could not store file fragment in cache, not writing shortcut info");
            return;
          } else {
            if ((stored_blob_bytes += st.st_size - inherited_file.start_offset) > max_entry_size) {
              FB_DEBUG(FB_DEBUG_CACHING,
                       "Could not store blob in cache because it would exceed max_entry_size");
              return;
            }
            FBBSTORE_Builder_append_to_fd& new_append = out_append_to_fd.emplace_back();
            new_append.set_fd(fd);
            new_append.set_hash(hash.get());
          }
        }
      }
    }
  }

  /* Validate cache entry to be stored. */
  if (!consistent_implicit_parent_dirs(out_path_isreg,
                                       out_path_isdir_filename_ptrs,
                                       proc->file_usages())) {
    proc->disable_shortcutting_only_this(
        "Inconsistency: A parent dir of an output file must not exit for shortcutting.");
    return;
  }

  /* Sort the entries for better cache compression ratios and easier debugging.
   *
   * Note that previously we carefully collected the inputs and outputs in system and non-system
   * locations separately for performance reasons. Here the outputs are mixed because they are not
   * likely to be in system locations, but inputs are still separated to system and non-system
   * locations. */
  struct {
    bool operator()(const FBBSTORE_Builder_file& a, const FBBSTORE_Builder_file& b) const {
      return strcmp(a.get_path(), b.get_path()) < 0;
    }
  } file_less;
  /* Sort non-system and system paths separately to not regress in shortcutting performance. */
  std::sort(in_path.begin(), in_path.begin() + in_path_non_system_count, file_less);
  std::sort(in_path.begin() + in_path_non_system_count, in_path.end(), file_less);
  std::sort(out_path_isreg.begin(), out_path_isreg.end(), file_less);
  std::sort(out_path_isdir.begin(), out_path_isdir.end(), file_less);

  struct {
    bool operator()(const cstring_view& a, const cstring_view& b) const {
      return strcmp(a.c_str, b.c_str) < 0;
    }
  } cstring_view_less;
  /* Sort non-system and system paths separately to not regress in shortcutting performance. */
  std::sort(in_path_notexist.begin(),
            in_path_notexist.begin() + in_path_notexist_non_system_count, cstring_view_less);
  std::sort(in_path_notexist.begin() + in_path_notexist_non_system_count,
            in_path_notexist.end(), cstring_view_less);

  std::sort(out_path_notexist.begin(), out_path_notexist.end());

  pi.set_path_item_fn(in_path.size(), file_item_fn, &in_path);
  pi.set_path_notexist(in_path_notexist);
  po.set_path_isreg_item_fn(out_path_isreg.size(), file_item_fn, &out_path_isreg);
  po.set_path_isdir_item_fn(out_path_isdir.size(), file_item_fn, &out_path_isdir);
  po.set_path_notexist(out_path_notexist);
  po.set_append_to_fd_item_fn(out_append_to_fd.size(), fbbstore_builder_append_to_fd_vector_item_fn,
                              &out_append_to_fd);
  po.set_exit_status(proc->fork_point()->exit_status());

  // TODO(egmont) Add all sorts of other stuff

  FBBSTORE_Builder_process_inputs_outputs pio;
  pio.set_inputs(reinterpret_cast<FBBSTORE_Builder *>(&pi));
  pio.set_outputs(reinterpret_cast<FBBSTORE_Builder *>(&po));
  if (!FB_DEBUGGING(FB_DEBUG_DETERMINISTIC_CACHE)) {
    pio.set_cpu_time_ms((proc->aggr_cpu_time_u() / 1000) + proc->shortcut_cpu_time_ms());
  }

  const FBBFP_Serialized *debug_msg = NULL;
  if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
    debug_msg = reinterpret_cast<const FBBFP_Serialized *>(fingerprint_msgs_[proc].data());
  }

  /* Store in the cache everything about this process. */
  const Hash fingerprint = fingerprints_[proc];
  obj_cache->store(fingerprint, reinterpret_cast<FBBSTORE_Builder *>(&pio), stored_blob_bytes,
                   debug_msg);
}

void ExecedProcessCacher::update_cached_bytes(off_t bytes) {
  this_runs_cached_bytes_ += bytes;
#ifdef FB_EXTRA_DEBUG
  off_t total = obj_cache->gc_collect_total_objects_size()
      + blob_cache->gc_collect_total_blobs_size();
  off_t stored = get_stored_bytes_from_cache();
  FB_DEBUG(FB_DEBUG_CACHING, " Cache-size real: " + d(total)
           + " calculated: " + d(stored + this_runs_cached_bytes_)
           + " stored: " + d(stored));
  assert_cmp(total, ==, stored + this_runs_cached_bytes_);
#endif
}

/**
 * Create a FileInfo object based on an FBB's File entry.
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
  info.set_mode_bits(file->get_mode_with_fallback(0), file->get_mode_mask_with_fallback(0));
  return info;
}

/**
 * Create a FileUsageUpdate object based on an FBB's File entry.
 */
static FileUsageUpdate file_to_file_usage_update(const FileName *filename,
                                                 const FBBSTORE_Serialized_file *file) {
  bool written = (file->get_type() == ISREG && file->has_size()) ||
                 (file->get_type() == ISDIR);
  bool mode_changed = file->has_mode();
  /* "file" represents the file's _new_ state, so it's not suitable for the _initial_ state of the
   * update. The initial state can be anything, i.e. DONTKNOW. */
  FileUsageUpdate update(filename, DONTKNOW, written, mode_changed);
  return update;
}

static const FBBSTORE_Serialized_file* find_input_file(const FBBSTORE_Serialized_process_inputs *pi,
                                                       const FileName* path) {
  for (size_t i = 0; i < pi->get_path_count(); i++) {
    auto file = reinterpret_cast<const FBBSTORE_Serialized_file *>(pi->get_path_at(i));
    if (FileName::Get(file->get_path(), file->get_path_len()) == path) {
      return file;
    }
  }
  return nullptr;
}

/**
 * Check whether the given process inputs match the file system's current contents
 * and the outputs are likely applicable.
 */
static bool pio_matches_fs(const FBBSTORE_Serialized_process_inputs_outputs *candidate_inouts,
                           const char* const subkey) {
  TRACK(FB_DEBUG_PROC, "subkey=%s", D(subkey));

  const FBBSTORE_Serialized *inputs_fbb = candidate_inouts->get_inputs();
  assert_cmp(inputs_fbb->get_tag(), ==, FBBSTORE_TAG_process_inputs);
  auto inputs =
      reinterpret_cast<const FBBSTORE_Serialized_process_inputs *>(inputs_fbb);

  size_t i;

  for (i = 0; i < inputs->get_path_count(); i++) {
    auto file = reinterpret_cast<const FBBSTORE_Serialized_file *>(inputs->get_path_at(i));
    const auto path = FileName::Get(file->get_path(), file->get_path_len());
    const FileInfo query = file_to_file_info(file);
    if (!hash_cache->file_info_matches(path, query)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT, "│   " + d(subkey) + " mismatches e.g. at " + d(path));
      return false;
    }
  }

  for (i = 0; i < inputs->get_path_notexist_count(); i++) {
    const auto path = FileName::Get(inputs->get_path_notexist_at(i),
                                    inputs->get_path_notexist_len_at(i));
    const FileInfo query(NOTEXIST);
    if (!hash_cache->file_info_matches(path, query)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   " + d(subkey)
               + " mismatches e.g. at " + d(path)
               + ": path expected to be missing, existing object is found");
      return false;
    }
  }

  const FBBSTORE_Serialized_process_outputs *outputs =
      reinterpret_cast<const FBBSTORE_Serialized_process_outputs *>
      (candidate_inouts->get_outputs());

  /* Check if shortcut is applicable, i.e. outputs can be created/can be written, etc. */
  // TODO(rbalint) extend these checks
  for (i = 0; i < outputs->get_path_isreg_count(); i++) {
    auto file = reinterpret_cast<const FBBSTORE_Serialized_file *>(outputs->get_path_isreg_at(i));
    if (file->get_type() == ISREG && access(file->get_path(), W_OK) == -1) {
      if (errno == EACCES) {
        /* The regular file can't be written, let's see if that was expected. */
        const auto path = FileName::Get(file->get_path(), file->get_path_len());
        const FBBSTORE_Serialized_file* input_file = find_input_file(inputs, path);
        if (input_file && (file_to_file_info(file).mode_mask() & 0200)) {
          /* The file has already been checked to be not writable and will be replaced while
           * applying the shortcut. */
        } else {
          return false;
        }
      }
    }
  }
  return true;
}

const FBBSTORE_Serialized_process_inputs_outputs * ExecedProcessCacher::find_shortcut(
    const ExecedProcess *proc,
    uint8_t **inouts_buf,
    size_t *inouts_buf_len,
    Subkey* subkey_out) {
  TRACK(FB_DEBUG_PROC, "proc=%s", D(proc));

  const FBBSTORE_Serialized_process_inputs_outputs *inouts = nullptr;
  int shortcut_attempts {0};
#ifdef FB_EXTRA_DEBUG
  int count = 0;
#endif
  Hash fingerprint = fingerprints_[proc];  // FIXME error handling

  FB_DEBUG(FB_DEBUG_SHORTCUT, "│ Candidates:");
  const std::vector<Subkey> subkeys = obj_cache->list_subkeys(fingerprint);
  if (subkeys.empty()) {
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   None found");
  }
  for (const Subkey& subkey : subkeys) {
    uint8_t *candidate_inouts_buf;
    size_t candidate_inouts_buf_len;
    if (shortcut_attempts++ > shortcut_tries) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│  Maximum shortcutting attempts (" + d(shortcut_tries) + ") exceeded, giving up");
      break;
    }
    if (!obj_cache->retrieve(fingerprint, subkey.c_str(),
                             &candidate_inouts_buf, &candidate_inouts_buf_len)) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   Cannot retrieve " + d(subkey) + " from objcache, ignoring");
      continue;
    }
    auto candidate_inouts_fbb = reinterpret_cast<const FBBSTORE_Serialized *>(candidate_inouts_buf);
    assert_cmp(candidate_inouts_fbb->get_tag(), ==, FBBSTORE_TAG_process_inputs_outputs);
    auto candidate_inouts =
        reinterpret_cast<const FBBSTORE_Serialized_process_inputs_outputs *>(candidate_inouts_fbb);

    if (pio_matches_fs(candidate_inouts, subkey.c_str())) {
      FB_DEBUG(FB_DEBUG_SHORTCUT, "│   " + d(subkey) + " matches the file system");
#ifdef FB_EXTRA_DEBUG
      count++;
      if (count == 1) {
#endif
        *inouts_buf = candidate_inouts_buf;
        *inouts_buf_len = candidate_inouts_buf_len;
        *subkey_out = subkey;
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
      auto file1 = reinterpret_cast<const FBBSTORE_Serialized_file *>(file1_generic);
      const FBBSTORE_Serialized *file2_generic = outputs->get_path_isdir_at(i2);
      auto file2 = reinterpret_cast<const FBBSTORE_Serialized_file *>(file2_generic);
      return file1->get_path_len() < file2->get_path_len();
    }
  } pathname_length_less;
  pathname_length_less.outputs = outputs;
  std::sort(indices.begin(), indices.end(), pathname_length_less);
  /* Process the directory names in ascending order of their lengths */
  for (i = 0; i < outputs->get_path_isdir_count(); i++) {
    const FBBSTORE_Serialized *dir_generic = outputs->get_path_isdir_at(indices[i]);
    assert_cmp(dir_generic->get_tag(), ==, FBBSTORE_TAG_file);
    auto dir = reinterpret_cast<const FBBSTORE_Serialized_file *>(dir_generic);
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
      FileUsageUpdate update = file_to_file_usage_update(path, dir);
      proc->parent_exec_point()->register_file_usage_update(path, update);
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
      // FIXME Register that it was an _empty_ directory
      FileUsageUpdate update = FileUsageUpdate(path, ISDIR, true);
      proc->parent_exec_point()->register_file_usage_update(path, update);
    }
  }
}

static void close_all(std::vector<int>* fds) {
  for (int fd : *fds) {
    close(fd);
  }
}

static bool add_blob_fd_from_hash(const XXH128_hash_t& fbb_hash, std::vector<int>* blob_fds) {
  Hash hash(fbb_hash);
  int fd;
  if ((fd = blob_cache->get_fd_for_file(hash)) != -1) {
    blob_fds->push_back(fd);
  } else {
    close_all(blob_fds);
    return false;
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
                                         const FBBSTORE_Serialized_process_inputs_outputs *inouts,
                                         std::vector<int> *fds_appended_to) {
  TRACK(FB_DEBUG_PROC, "proc=%s", D(proc));

  size_t i;
  std::vector<int> blob_fds;

  const FBBSTORE_Serialized_process_outputs *outputs =
      reinterpret_cast<const FBBSTORE_Serialized_process_outputs *>
      (inouts->get_outputs());

  /* Pre-open blobs to be used later to prevent a parallel GC run from removing them while
   * shortcutting. */
  for (i = 0; i < outputs->get_path_isreg_count(); i++) {
    auto file = reinterpret_cast<const FBBSTORE_Serialized_file *>(outputs->get_path_isreg_at(i));
    if (file->get_type() == ISREG) {
      assert(file->has_hash());
      if (!add_blob_fd_from_hash(file->get_hash(), &blob_fds)) {
        return false;
      }
    }
  }
  for (i = 0; i < outputs->get_append_to_fd_count(); i++) {
    auto append_to_fd = reinterpret_cast<const FBBSTORE_Serialized_append_to_fd *>
        (outputs->get_append_to_fd_at(i));
    if (!add_blob_fd_from_hash(append_to_fd->get_hash(), &blob_fds)) {
      return false;
    }
  }

  /* Bubble up all the file operations we're about to perform. */
  if (proc->parent_exec_point()) {
    const FBBSTORE_Serialized_process_inputs *inputs =
        reinterpret_cast<const FBBSTORE_Serialized_process_inputs *>
        (inouts->get_inputs());

    for (i = 0; i < inputs->get_path_count(); i++) {
      auto file = reinterpret_cast<const FBBSTORE_Serialized_file *>(inputs->get_path_at(i));
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

  if (!restore_dirs(proc, outputs)) {
    close_all(&blob_fds);
    return false;
  }

  size_t next_blob_fd_idx = 0;
  for (i = 0; i < outputs->get_path_isreg_count(); i++) {
    auto file = reinterpret_cast<const FBBSTORE_Serialized_file *>(outputs->get_path_isreg_at(i));
    const auto path = FileName::Get(file->get_path(), file->get_path_len());
    if (file->get_type() == ISREG) {
      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   Fetching file from blobs cache: "
               + d(path));
      assert(file->has_hash());
      Hash hash(file->get_hash());
      if (!blob_cache->retrieve_file(blob_fds[next_blob_fd_idx++], path, false)) {
        /* The file may not be writable but it may be expected and already checked. */
        const FBBSTORE_Serialized_file* input_file =
            find_input_file(
                reinterpret_cast<const FBBSTORE_Serialized_process_inputs *>(inouts->get_inputs()),
                path);
        if (errno == EACCES && input_file && (file_to_file_info(file).mode_mask() & 0200)) {
          /* The file has already been checked to be not writable and should be completely replaced
           * from the cache. Let's remove it and try again. */
          if (unlink(path->c_str()) == -1) {
            fb_perror("Failed removing file to be replaced from cache");
            assert(0);
          }
          /* Try retrieving the same file again. */
          if (!blob_cache->retrieve_file(blob_fds[next_blob_fd_idx - 1], path, false)) {
            fb_perror("Failed creating file from cache");
            assert(0);
          }
        } else {
          fb_perror("Failed opening file to be recreated from cache");
          assert(0);
        }
      }
    }
    if (file->has_mode()) {
      /* Refuse to apply setuid, setgid, sticky bit. */
      // FIXME warn on them, even when we store them.
      chmod(path->c_str(), file->get_mode() & 0777);
    }
    if (proc->parent_exec_point()) {
      FileUsageUpdate update = file_to_file_usage_update(path, file);
      proc->parent_exec_point()->register_file_usage_update(path, update);
    }
  }

  remove_files_and_dirs(proc, outputs);

  /* See what the process originally wrote to its inherited files (pipes or regular files).
   * Replay these. */
  for (i = 0; i < outputs->get_append_to_fd_count(); i++) {
    auto append_to_fd = reinterpret_cast<const FBBSTORE_Serialized_append_to_fd *>
        (outputs->get_append_to_fd_at(i));
    FileFD *ffd = proc->get_fd(append_to_fd->get_fd());
    assert(ffd);

    if (ffd->type() == FD_PIPE_OUT) {
      Pipe *pipe = ffd->pipe().get();
      assert(pipe);

      Hash hash(append_to_fd->get_hash());
      int fd = blob_fds[next_blob_fd_idx++];
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
    } else if (ffd->type() == FD_FILE) {
      assert(ffd->filename());

      FB_DEBUG(FB_DEBUG_SHORTCUT,
               "│   Fetching file fragment from blobs cache: "
               + d(ffd->filename()));
      Hash hash(append_to_fd->get_hash());
      blob_cache->retrieve_file(blob_fds[next_blob_fd_idx++], ffd->filename(), true);

      /* Tell the interceptor to seek forward in this fd. */
      fds_appended_to->push_back(ffd->fd());
    } else {
      assert(0 && "wrong file_fd type");
    }

    /* Bubble up the event that we wrote to this inherited fd. Currently this doesn't do anything,
     * but it might change in the future. */
    proc->handle_write_to_inherited(ffd->fd(), false);
  }

  /* Set the exit code, propagate upwards. */
  // TODO(egmont) what to do with resource usage?
  proc->fork_point()->set_exit_status(outputs->get_exit_status());

  close_all(&blob_fds);
  return true;
}

/**
 * Tries to shortcut the process.
 *
 * Returns if it succeeded.
 */
bool ExecedProcessCacher::shortcut(ExecedProcess *proc, std::vector<int> *fds_appended_to) {
  TRACK(FB_DEBUG_PROC, "proc=%s", D(proc));

  if (no_fetch_) {
    return false;
  }

  shortcut_attempts_++;

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

  Subkey subkey;
  if (proc->can_shortcut()) {
    inouts = find_shortcut(proc, &inouts_buf, &inouts_buf_len, &subkey);
  }

  FB_DEBUG(FB_DEBUG_SHORTCUT, inouts ? "│ Shortcutting:" : "│ Not shortcutting.");

  if (inouts) {
    ret = apply_shortcut(proc, inouts, fds_appended_to);
    FB_DEBUG(FB_DEBUG_SHORTCUT, "│   Exiting with " + d(proc->fork_point()->exit_status()));
    if (ret) {
      Hash fp = fingerprints_[proc];
      obj_cache->mark_as_used(fp, subkey.c_str());
      shortcut_hits_++;
      if (inouts->has_cpu_time_ms()) {
        proc->add_shortcut_cpu_time_ms(inouts->get_cpu_time_ms());
      }
    }
    /* Trigger cleanup of ProcessInputsOutputs. */
    inouts = nullptr;
    munmap(inouts_buf, inouts_buf_len);
  }
  FB_DEBUG(FB_DEBUG_SHORTCUT, "└─");

  proc->set_was_shortcut(ret);
  return ret;
}

/**
 * Checks if the blob is present in the blob cache and saves existing blobs' hash to
 * referenced_blobs. */
static bool blob_present(const Hash& hash, tsl::hopscotch_set<AsciiHash>* referenced_blobs) {
  char ascii_hash_buf[Hash::kAsciiLength + 1];
  hash.to_ascii(ascii_hash_buf);
  AsciiHash ascii_hash {ascii_hash_buf};
  if (referenced_blobs->find(ascii_hash) == referenced_blobs->end()) {
    int fd = blob_cache->get_fd_for_file(hash);
    if (fd == -1) {
      FB_DEBUG(FB_DEBUG_CACHING,
               "Cache entry contains reference to an output blob missing from the cache: " +
               d(ascii_hash));
      FB_DEBUG(FB_DEBUG_CACHING, "The cache may have been corrupted.");
      return false;
    } else {
      // TODO(rbalint) validate content's hash
      close(fd);
      referenced_blobs->insert(ascii_hash);
    }
  }
  return true;
}

bool ExecedProcessCacher::is_entry_usable(uint8_t* entry_buf,
                                          tsl::hopscotch_set<AsciiHash>* referenced_blobs) {
  auto inouts_fbb = reinterpret_cast<const FBBSTORE_Serialized *>(entry_buf);
  if (inouts_fbb->get_tag() != FBBSTORE_TAG_process_inputs_outputs) {
    return false;
  }
  auto inouts = reinterpret_cast<const FBBSTORE_Serialized_process_inputs_outputs *>(inouts_fbb);

  const FBBSTORE_Serialized *inputs_fbb = inouts->get_inputs();
  if (inputs_fbb->get_tag() != FBBSTORE_TAG_process_inputs) {
    return false;
  }
  auto inputs = reinterpret_cast<const FBBSTORE_Serialized_process_inputs *>(inputs_fbb);

  /* Check existing regular system files files.
   * Only existing ones because --gc may be run when some build dependencies are missing which
   * would be installed before CI runs where firebuild is in use.
   */
  for (size_t i = 0; i < inputs->get_path_count(); i++) {
    auto file = reinterpret_cast<const FBBSTORE_Serialized_file *>(inputs->get_path_at(i));
    const auto path {FileName::Get(file->get_path(), file->get_path_len())};
    const FileInfo query {file_to_file_info(file)};
    if (query.type() == ISREG && path->is_in_read_only_location() &&
        !hash_cache->file_info_matches(path, query) &&
        hash_cache->file_info_matches(path, FileInfo(EXIST))) {
      FB_DEBUG(FB_DEBUG_CACHING, "Cache entry expects a system file that has changed: " + d(path));
      return false;
    }
  }
  /* The entry seems to be valid, collect the referenced blobs. */
  auto outputs =
      reinterpret_cast<const FBBSTORE_Serialized_process_outputs *>(inouts->get_outputs());
  for (size_t i = 0; i < outputs->get_path_isreg_count(); i++) {
    auto file = reinterpret_cast<const FBBSTORE_Serialized_file *>(outputs->get_path_isreg_at(i));
    if (file->get_type() == ISREG && file->has_hash()) {
      if (!blob_present(Hash(file->get_hash()), referenced_blobs)) {
        return false;
      }
    }
  }
  for (size_t i = 0; i < outputs->get_append_to_fd_count(); i++) {
    auto append_to_fd = reinterpret_cast<const FBBSTORE_Serialized_append_to_fd *>
        (outputs->get_append_to_fd_at(i));
    if (!blob_present(Hash(append_to_fd->get_hash()), referenced_blobs)) {
      return false;
    }
  }
  return true;
}

static void print_time(FILE* f, const int time_ms) {
  double time = time_ms;
  if (time_ms < 0) {
    fprintf(f, "-");
    time = -time_ms;
  }
  if (time < 1000) {
    fprintf(f, "%.0f ms", time);
    return;
  }
  time /= 1000;
  if (time < 60) {
    fprintf(f, "%.2f seconds", time);
    return;
  }
  time /= 60;
  if (time < 60) {
    fprintf(f, "%.2f minutes", time);
    return;
  }
  time /= 60;
  if (time < 24) {
    fprintf(f, "%.2f hours", time);
    return;
  }
  time /= 24;
  if (time < 7) {
    fprintf(f, "%.2f days", time);
    return;
  }
  time /= 7;
  fprintf(f, "%.2f weeks", time);
}

static void print_bytes(FILE* f, const off_t bytes) {
  double size = bytes;
  if (size < 0) {
    fprintf(f, "-");
    size = -size;
  }
  size /= 1000;
  if (size < 1000) {
    fprintf(f, "%.2f kB", size);
    return;
  }
  size /= 1000;
  if (size < 1000) {
    fprintf(f, "%.2f MB", size);
    return;
  }
  size /= 1000;
  fprintf(f, "%.2f GB", size);
}

void ExecedProcessCacher::print_stats(stats_type what) {
  printf("Statistics of %s:\n", what == FB_SHOW_STATS_CURRENT ? "current run" : "stored cache");
  printf("  Hits:        %6u / %u (%.2f %%)\n", shortcut_hits_, shortcut_attempts_,
         shortcut_attempts_ > 0 ? (static_cast<float>(100 * shortcut_hits_) / shortcut_attempts_) :
         0);
  printf("  Misses:      %6u\n", shortcut_attempts_ - shortcut_hits_);
  printf("  Uncacheable: %6u\n", not_shortcutting_);
  printf("  GC runs:     %6u\n", gc_runs_);
  if (what == FB_SHOW_STATS_CURRENT) {
    printf("Newly cached:  ");
    print_bytes(stdout, this_runs_cached_bytes_);
  } else {
    printf("Cache size:    ");
    print_bytes(stdout, get_stored_bytes_from_cache());
  }
  printf("\n");
  printf("Saved CPU time:  ");
  print_time(stdout, cache_saved_cpu_time_ms_ - self_cpu_time_ms_ +
             (proc_tree ? proc_tree->shortcut_cpu_time_ms() : 0));
  printf("\n");
}

void ExecedProcessCacher::add_stored_stats() {
  /* Read cache statistics. */
  FILE* f;
  unsigned int shortcut_attempts, shortcut_hits, not_shortcutting, gc_runs;
  const std::string stats_file = cache_dir_ + "/" + kCacheStatsFile;
  if ((f = fopen(stats_file.c_str(), "r"))) {
    if (fscanf(f, "attempts: %u\nhits: %u\nskips: %u\ngc_runs: %u\nsaved_cpu_ms: %" SCNd64 "\n",
               &shortcut_attempts, &shortcut_hits, &not_shortcutting, &gc_runs,
               &cache_saved_cpu_time_ms_) != 5) {
      fb_error("Invalid stats file format at " + stats_file + ", using only current run's stats.");
    } else {
      shortcut_attempts_ += shortcut_attempts;
      shortcut_hits_ += shortcut_hits;
      not_shortcutting_ += not_shortcutting;
      gc_runs_ += gc_runs;
    }
    fclose(f);
  }
}

void ExecedProcessCacher::reset_stored_stats() {
  const std::string stats_file = cache_dir_ + "/" + kCacheStatsFile;
  if (unlink(stats_file.c_str()) == -1 && errno != ENOENT) {
    fb_perror("removing stats file failed");
    exit(EXIT_FAILURE);
  }
}

void ExecedProcessCacher::update_stored_stats() {
  // FIXME(rbalint) There is a slight chance for two parallel builds updating the stats at the
  // same time making them inaccurate.
  add_stored_stats();
  const std::string stats_file = cache_dir_ + "/" + kCacheStatsFile;
  if (file_overwrite_printf(
          stats_file, "attempts: %u\nhits: %u\nskips: %u\ngc_runs: %u\nsaved_cpu_ms: %" PRId64 "\n",
          shortcut_attempts_, shortcut_hits_, not_shortcutting_, gc_runs_,
          cache_saved_cpu_time_ms_ - self_cpu_time_ms_ +
          (proc_tree ? proc_tree->shortcut_cpu_time_ms() : 0)) < 0) {
    fb_error("writing cache stats file failed");
    exit(EXIT_FAILURE);
  }
}

off_t ExecedProcessCacher::get_stored_bytes_from_cache() const {
  FILE* f;
  const std::string size_file = cache_dir_ + "/" + kCacheSizeFile;
  off_t cached_bytes = 0;
  if ((f = fopen(size_file.c_str(), "r"))) {
    if (fscanf(f, "%" SCNoff "\n", &cached_bytes) != 1) {
      fb_error("Invalid size file format in " + size_file + ", fixing it.");
      fclose(f);
      return fix_stored_bytes();
    }
    fclose(f);
  }
  if (cached_bytes < 0) {
    fb_error("Invalid size in " + size_file + ", fixing it.");
    cached_bytes = fix_stored_bytes();
  }
  return cached_bytes;
}

void ExecedProcessCacher::read_stored_cached_bytes() {
  stored_cached_bytes_ = get_stored_bytes_from_cache();
}

void ExecedProcessCacher::update_stored_bytes() {
  // FIXME(rbalint) There is a slight chance for two parallel builds updating the size at the
  // same time making the file content inaccurate.
  const std::string size_file = cache_dir_ + "/" + kCacheSizeFile;
  const off_t new_size = this_runs_cached_bytes_ + stored_cached_bytes_;
  if (file_overwrite_printf(size_file, "%ld\n", new_size) < 0) {
    fb_error("writing cache size file failed");
    exit(EXIT_FAILURE);
  }
}

off_t ExecedProcessCacher::fix_stored_bytes() const {
  // FIXME(rbalint) There is a slight chance for two parallel builds updating the size at the
  // same time making the file content inaccurate.
  const std::string size_file = cache_dir_ + "/" + kCacheSizeFile;
  off_t starting_cached_bytes =  obj_cache->gc_collect_total_objects_size()
      + blob_cache->gc_collect_total_blobs_size() - this_runs_cached_bytes_;
  if (file_overwrite_printf(size_file, "%ld\n", starting_cached_bytes) < 0) {
    fb_error("writing cache size file failed");
    exit(EXIT_FAILURE);
  }
  return starting_cached_bytes;
}

bool ExecedProcessCacher::is_gc_needed() const {
  return (get_stored_bytes_from_cache() + this_runs_cached_bytes_) > max_cache_size;
}

void ExecedProcessCacher::gc() {
  gc_runs_++;
  /* Remove unusable entries first. */
  tsl::hopscotch_set<AsciiHash> referenced_blobs {};
  off_t cache_bytes = 0, debug_bytes = 0, unexpected_file_bytes = 0;
  obj_cache->gc(&referenced_blobs, &cache_bytes, &debug_bytes, &unexpected_file_bytes);
  blob_cache->gc(referenced_blobs, &cache_bytes, &debug_bytes, &unexpected_file_bytes);
  if (unexpected_file_bytes > 0) {
    fb_error("There are " + d(unexpected_file_bytes) + " bytes in the cache stored in files "
             "with unexpected name.");
  }
  stored_cached_bytes_ = cache_bytes + debug_bytes - this_runs_cached_bytes_;
  if (FB_DEBUGGING(FB_DEBUG_CACHING)) {
    if (cache_bytes + debug_bytes != this_runs_cached_bytes_ + get_stored_bytes_from_cache()) {
      FB_DEBUG(FB_DEBUG_CACHING, "A parallel firebuild process modified the cache or the stored "
               "cache size was wrong. Adjusting the stored cache size.");
    }
  }

  /* Check if the cache size is within limits. */
  if (stored_cached_bytes_ + this_runs_cached_bytes_ > max_cache_size) {
    FB_DEBUG(FB_DEBUG_CACHING,
             "Cache size (" + d(stored_cached_bytes_ + this_runs_cached_bytes_) + ") " +
             "is above " + d(max_cache_size) + " bytes limit, removing older entries");
    /** Target for this_runs_cached_bytes_ to end up with a cache 20% below its size limit. */
    const off_t target_this_runs_cached_bytes = (max_cache_size * 0.8) - stored_cached_bytes_;
    std::vector<obj_timestamp_size_t> obj_ts_sizes =
        obj_cache->gc_collect_sorted_obj_timestamp_sizes();
    int round = 0;
    while (this_runs_cached_bytes_ > target_this_runs_cached_bytes) {
      /* Set kept_ratio to to keep ~80% of the objs to keep ~80% of targeted cache size
       * and target lower kept ratio in each round if the target 80% is not reached. */
      const double kept_ratio = (max_cache_size * (0.8 - round * 0.05))
          / (stored_cached_bytes_ + this_runs_cached_bytes_);
      if (kept_ratio <= 0.0) {
        break;
      }
      off_t keep_objects_count = obj_ts_sizes.size() * kept_ratio;
      FB_DEBUG(FB_DEBUG_CACHING, "Removing " + d(obj_ts_sizes.size() - keep_objects_count) + " " +
               "cache objects out of " + d(obj_ts_sizes.size()));
      for (size_t i = keep_objects_count; i < obj_ts_sizes.size(); i++) {
        const char* name = obj_ts_sizes[i].obj.c_str();
        if (unlink(name) != 0) {
          fb_error(name);
          fb_perror("unlink");
        } else {
          update_cached_bytes(-obj_ts_sizes[i].size);
        }
      }
      obj_ts_sizes.resize(keep_objects_count);

      /* Remove unreferenced blobs, too. */
      referenced_blobs.clear();

      /* Not adjusting the stored cache size this time. */
      cache_bytes = debug_bytes = unexpected_file_bytes = 0;
      obj_cache->gc(&referenced_blobs, &cache_bytes, &debug_bytes, &unexpected_file_bytes);
      blob_cache->gc(referenced_blobs, &cache_bytes, &debug_bytes, &unexpected_file_bytes);

      round++;
    }
  }
}

}  /* namespace firebuild */
