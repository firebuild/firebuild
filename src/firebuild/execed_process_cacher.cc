/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include "firebuild/execed_process_cacher.h"

#include "firebuild/debug.h"
#include "firebuild/execed_process.h"
#include "firebuild/fb-cache.pb.h"

namespace firebuild {

ExecedProcessCacher::ExecedProcessCacher(Cache *cache,
                                         MultiCache *multi_cache) :
    cache_(cache), multi_cache_(multi_cache) { }

void ExecedProcessCacher::store(const ExecedProcess *proc) {
  /* Go through the files the process opened for reading and/or writing.
   * Construct the protobuf describing the initial and the final state
   * of them. */
  firebuild::msg::ProcessInputsOutputs pio;
  std::map<std::string, FileUsage*> sorted_file_usages(proc->file_usages().begin(),
                                                       proc->file_usages().end());
  for (const auto& pair : sorted_file_usages) {
    const std::string& filename = pair.first;
    const FileUsage* fu = pair.second;

    /* If the file's initial contents matter, record it in pb's "inputs".
     * This is purely data conversion from one format to another. */
    switch (fu->initial_state()) {
      case DONTCARE:
        /* Nothing to do. */
        break;
      case EXIST_WITH_HASH: {
        firebuild::msg::File* input = pio.mutable_inputs()->add_file_exist_with_hash();
        input->set_path(filename);
        input->set_hash(fu->initial_hash().to_binary());
        break;
      }
      case EXIST:
        pio.mutable_inputs()->add_file_exist(filename);
        break;
      case NOTEXIST_OR_EMPTY:
        pio.mutable_inputs()->add_file_notexist_or_empty(filename);
        break;
      case NOTEXIST:
        pio.mutable_inputs()->add_file_notexist(filename);
        break;
      default:
        assert(false);
    }

    /* If the file's final contents matter, place it in the file cache,
     * and also record it in pb's "outputs". This actually needs to
     * compute the checksums now. */
    if (fu->written()) {
      Hash hash;
      if (cache_->store_file(filename, &hash)) {
        /* FIXME don't store and don't record if it was read with the same hash. */
        struct stat st;
        if (stat(filename.c_str(), &st) == -1) {
          /* unexpected error, now what? */
          st.st_mode = 0;
        }
        firebuild::msg::File* file_written = pio.mutable_outputs()->add_file_with_hash();
        file_written->set_path(filename);
        file_written->set_hash(hash.to_binary());
        // TODO fail if setuid/setgid/sticky is set
        file_written->set_mode(st.st_mode & 07777);
      } else {
        if (fu->initial_state() != NOTEXIST) {
          pio.mutable_outputs()->add_file_notexist(filename);
        }
      }
    }
  }

  pio.mutable_outputs()->set_exit_status(proc->exit_status());
  // TODO Add all sorts of other stuff

  std::string debug_header;
  if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
    debug_header += pretty_print_timestamp() + "\n\n";
    debug_header += "exe: " + pretty_print_string(proc->executable()) + "\n";
    debug_header += "arg: " + pretty_print_array(proc->args()) + "\n";
    debug_header += "env: " + pretty_print_array(proc->env_vars()) + "\n\n";
    // FIXME add initial cwd and stuff
  }

  /* Store in the cache everything about this process. */
  multi_cache_->store_protobuf(proc->fingerprint(), pio, debug_header, NULL);
}

}  // namespace firebuild
