/* Copyright (c) 2020 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

/*
 * multi-cache is a weird caching structure where a key can contain
 * multiple values. More precisely, a key contains a list of subkeys,
 * and a (key, subkey) pair points to a value.
 *
 * In practice, one ProcessDescription can have multiple
 * ProcessInputsOutputs associated with it. The key is the hash of
 * ProcessDescription's serialization. The subkey happens to be the hash
 * of ProcessInputsOutputs's serialization, although it could easily be
 * anything else.
 *
 * Currently the backend is the filesystem. The multiple values are
 * stored as separate file of a given directory. The list of subkeys is
 * retrieved by listing the directory.
 *
 * E.g. ProcessDescription1's hash in hex is "description1". Underneath
 * it there are two values: ProcessInputsOutputs1's hash in hex is
 * "inputsoutputs1",ProcessInputsOutputs2's hash in hex is
 * "inputsoutputs2". The directory structure is:
 * - d/de/description1/inputsoutputs1
 * - d/de/description1/inputsoutputs2
 */

#include "firebuild/multi_cache.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <google/protobuf/text_format.h>

#include "firebuild/debug.h"
#include "firebuild/hash.h"

namespace firebuild {

MultiCache::MultiCache(const std::string &base_dir) : base_dir_(base_dir) {
  mkdir(base_dir_.c_str(), 0700);
}

/*
 * Constructs the directory name where the cached files are to be
 * stored, or read from. Optionally creates the necessary subdirectories
 * within the cache's base directory.
 *
 * Example: with base="base", key's hex representation being "key", and
 * create_dirs=true, it creates the directories "base/k", "base/k/ke"
 * and "base/k/ke/key" and returns the latter.
 */
static std::string construct_cached_dir_name(const std::string &base,
                                             const Hash &key,
                                             bool create_dirs) {
  std::string key_str = key.to_hex();
  std::string path = base + "/" + key_str.substr(0, 1);
  if (create_dirs) {
    mkdir(path.c_str(), 0700);
  }
  path += "/" + key_str.substr(0, 2);
  if (create_dirs) {
    mkdir(path.c_str(), 0700);
  }
  path += "/" + key_str;
  if (create_dirs) {
    mkdir(path.c_str(), 0700);
  }
  return path;
}

/*
 * Constructs the filename where the cached file is to be stored, or
 * read from. Optionally creates the necessary subdirectories within the
 * cache's base directory.
 *
 * Example: with base="base", key's hex representation being "key",
 * subkey's hex representation being "subkey", and create_dirs=true, it
 * creates the directories "base/k", "base/k/ke" and "base/k/ke/key" and
 * returns "base/k/ke/key/subkey".
 */
static std::string construct_cached_file_name(const std::string &base,
                                              const Hash &key,
                                              const Hash &subkey,
                                              bool create_dirs) {
  std::string path = construct_cached_dir_name(base, key, create_dirs);
  return path + "/" + subkey.to_hex();
}

/**
 * A protobuf FieldValuePrinter that adds the hex hash to fields of
 * type "bytes" that happen to be exactly as long as our hashes, for
 * easier debugging. Other types are printed as usual.
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
};

/**
 * Store a protobuf (its serialization) in the protobuf cache.
 *
 * @param key The key
 * @param msg The protobuf to store
 * @param debug_header string prepended to debug lines
 * @param subkey_out Optionally store the subkey (hash of the protobuf) here
 * @return Whether succeeded
 */
bool MultiCache::store_protobuf(const Hash &key,
                                const google::protobuf::Message &msg,
                                const std::string &debug_header,
                                Hash *subkey_out) {
  if (firebuild::debug_level >= 2) {
    FB_DEBUG(2, "MultiCache: storing protobuf, key " + key.to_hex());
  }

  std::string tmpfile = base_dir_ + "/new.XXXXXX";
  char *tmpfile_c_writable = &*tmpfile.begin();  // hack against c_str()'s constness
  int fd_dst = mkstemp(tmpfile_c_writable);  // opens with O_RDWR
  if (fd_dst == -1) {
    perror("mkstemp");
    return false;
  }

  uint32_t msg_size = msg.ByteSize();
  uint8_t *buf = new uint8_t[msg_size];
  msg.SerializeWithCachedSizesToArray(buf);

  Hash subkey;
  subkey.set_from_data(buf, msg_size);

  // FIXME Do we need to handle short writes / EINTR?
  // FIXME Do we need to split large files into smaller writes?
  auto written = write(fd_dst, buf, msg_size);
  if (written != msg_size) {
    if (written == -1) {
      perror("write");
    } else {
      FB_DEBUG(2, "short write");
    }
    close(fd_dst);
    delete buf;
    return false;
  }
  close(fd_dst);

  delete buf;

  std::string path_dst = construct_cached_file_name(base_dir_, key, subkey, true);
  if (rename(tmpfile.c_str(), path_dst.c_str()) == -1) {
    perror("rename");
    unlink(tmpfile.c_str());
    return false;
  }

  if (subkey_out != NULL) {
    *subkey_out = subkey;
  }

  if (firebuild::debug_level >= 1) {
    FB_DEBUG(2, "  value hash " + subkey.to_hex());

    /* Place a human-readable version in the cache, for easier debugging. */
    std::string path_debug = path_dst + "_debug.txt";
    std::string pb_txt;

    const auto pb_hash_hex_value_printer = new ProtobufHashHexValuePrinter();
    google::protobuf::TextFormat::Printer printer;
    printer.SetDefaultFieldValuePrinter(pb_hash_hex_value_printer);
    printer.PrintToString(msg, &pb_txt);

    std::string txt = debug_header + pb_txt;
    int fd = creat(path_debug.c_str(), 0600);
    /* FIXME print some header about the process, e.g. argv */
    write(fd, txt.c_str(), txt.size());
    close(fd);
  }
  return true;
}

/**
 * Retrieve a protobuf from the protobuf cache.
 *
 * @param key The key
 * @param subkey The subkey
 * @param msg Where to store the protobuf
 * @return Whether succeeded
 */
bool MultiCache::retrieve_protobuf(const Hash &key,
                                   const Hash &subkey,
                                   google::protobuf::MessageLite *msg) {
  if (firebuild::debug_level >= 2) {
    FB_DEBUG(2, "MultiCache: retrieving protobuf, key " + key.to_hex() + " subkey " + subkey.to_hex());
  }

  std::string path = construct_cached_file_name(base_dir_, key, subkey, false);

  int fd = open(path.c_str(), O_RDONLY);
  if (fd == -1) {
    perror("open");
    return false;
  }

  struct stat64 st;
  if (fstat64(fd, &st) == -1) {
    perror("fstat");
    close(fd);
    return false;
  } else if (!S_ISREG(st.st_mode)) {
    FB_DEBUG(2, "not a regular file");
    close(fd);
    return false;
  }

  void *p = NULL;
  if (st.st_size > 0) {
    /* Zero bytes can't be mmapped, we're fine with p == NULL then.
     * Although a serialized protobuf probably can't be 0 bytes long. */
    p = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
      perror("mmap");
      close(fd);
      return false;
    }
  }

  bool ret = msg->ParseFromArray(p, st.st_size);

  munmap(p, st.st_size);
  close(fd);
  return ret;
}

/**
 * Return the list of subkeys for the given key.
 *
 * // FIXME return them in some particular order??
 *
 * // FIXME replace with some iterator-like approach?
 */
std::vector<Hash> MultiCache::list_subkeys(const Hash &key) {
  std::vector<Hash> ret;
  std::string path = construct_cached_dir_name(base_dir_, key, false);

  DIR *dir = opendir(path.c_str());
  if (dir == NULL) {
    return ret;
  }

  Hash subkey;
  struct dirent *dirent;
  while ((dirent = readdir(dir)) != NULL) {
    if (subkey.set_hash_from_hex(dirent->d_name)) {
      ret.push_back(subkey);
    }
  }

  closedir(dir);
  return ret;
}

}  // namespace firebuild
