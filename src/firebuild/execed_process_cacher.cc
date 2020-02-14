/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#include "firebuild/execed_process_cacher.h"

#include "firebuild/debug.h"
#include "firebuild/execed_process.h"
#include "firebuild/fb-cache.pb.h"

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
        sprintf(buf, "%02x", (unsigned char)(val[i]));
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

ExecedProcessCacher::ExecedProcessCacher(Cache *cache,
                                         MultiCache *multi_cache,
                                         bool no_store) :
    cache_(cache), multi_cache_(multi_cache), no_store_(no_store) { }

void ExecedProcessCacher::store(const ExecedProcess *proc) {
  if (no_store_) {
    return;
  }

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
      struct stat st;
      if (stat(filename.c_str(), &st) == 0) {
        /* TODO don't store and don't record if it was read with the same hash. */
        if (!cache_->store_file(filename, &hash)) {
          /* unexpected error, now what? */
          FB_DEBUG(FB_DEBUG_CACHE, "Could not store blob in cache, not writing shortcut info");
          return;
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
  google::protobuf::TextFormat::Printer *printer = NULL;
  if (FB_DEBUGGING(FB_DEBUG_CACHE)) {
    debug_header = pretty_print_timestamp() + "\n\n";

    const auto pb_hash_hex_value_printer = new ProtobufHashHexValuePrinter();
    printer = new google::protobuf::TextFormat::Printer();
    printer->SetDefaultFieldValuePrinter(pb_hash_hex_value_printer);  /* takes ownership */
  }

  /* Store in the cache everything about this process. */
  multi_cache_->store_protobuf(proc->fingerprint(), pio, proc->fingerprint_msg(), debug_header, printer, NULL);
  delete printer;
}

}  // namespace firebuild
