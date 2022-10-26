/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 */

#ifndef FIREBUILD_BASE64_H_
#define FIREBUILD_BASE64_H_

namespace firebuild {

/**
 * Base64 variant with encoding only.
 *
 * The two non-alphanumeric characters of our base64 alphabet are '+' and '^' and
 * none of the characters are at their usual position.
 * The characters are reordered compared to the original base64 mapping.
 * They are ordered by increasing ASCII code to have a set of hash values
 * and their ASCII representation sort the same way.
 * No trailing '=' signs to denote the partial block.
 */
class Base64 {
 public:
  static bool valid_ascii(const char* const str, const int length);
  static void encode(const unsigned char* in, char* out, int in_length);

 private:
  static void encode_3byte_block(const unsigned char *in, char *out);
  static void encode_2byte_block(const unsigned char *in, char *out);
  static void encode_1byte_block(const unsigned char *in, char *out);
  /* Subkey's sorting relies on the characters being in ASCII order. */
  static constexpr char kEncodeMap[] =
      "+0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ^abcdefghijklmnopqrstuvwxyz";
};

}  /* namespace firebuild */
#endif  // FIREBUILD_BASE64_H_
