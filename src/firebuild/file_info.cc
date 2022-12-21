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

#include "firebuild/file_info.h"

#include "common/firebuild_common.h"
#include "firebuild/debug.h"
#include "firebuild/hash.h"
#include "firebuild/hash_cache.h"

namespace firebuild {

/* Global debugging methods.
 * level is the nesting level of objects calling each other's d(), bigger means less info to print.
 * See #431 for design and rationale. */
std::string d(const FileInfo& fi, const int level) {
  (void)level;  /* unused */
  char mode_str[8], mode_mask_str[8];
  snprintf(mode_str, sizeof(mode_str), "0%03o", fi.mode());
  snprintf(mode_mask_str, sizeof(mode_mask_str), "0%03o", fi.mode_mask());
  return std::string("{FileInfo type=") +
      file_type_to_string(fi.type()) +
      (fi.size_known() ?
          ", size=" + d(fi.size()) : "") +
      (fi.hash_known() ?
          ", hash=" + d(fi.hash()) : "") +
      (fi.mode_mask() != 0 ?
          ", mode=" + std::string(mode_str) + ", mode_mask=" + std::string(mode_mask_str) : "") +
      "}";
}
std::string d(const FileInfo *fi, const int level) {
  if (fi) {
    return d(*fi, level);
  } else {
    return "{FileInfo NULL}";
  }
}

const char *file_type_to_string(FileType type) {
  switch (type) {
    case DONTKNOW:
      return "dontknow";
    case EXIST:
      return "exist";
    case NOTEXIST:
      return "notexist";
    case NOTEXIST_OR_ISREG:
      return "notexist_or_isreg";
    case ISREG:
      return "isreg";
    case ISDIR:
      return "isdir";
    default:
      assert(0 && "unknown type");
      return "UNKNOWN";
  }
}

}  /* namespace firebuild */
