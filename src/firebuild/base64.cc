/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 */

#include <firebuild/base64.h>

#include <firebuild/debug.h>

namespace firebuild {

bool Base64::valid_ascii(const char* const str, const int length) {
  int i;
  /* The last character could be from a limited set, depending on the length of the input
   * that's encoded in this base64-like ASCII representation. This is not checked here
   * because the ASCII representation is never decoded to get the input bits back in this
   * program. */
  for (i = 0; i < length; i++) {
    if ((str[i] >= 'A' && str[i] <= 'Z') ||
        (str[i] >= 'a' && str[i] <= 'z') ||
        (str[i] >= '0' && str[i] <= '9') ||
        str[i] == '+' || str[i] == '^') {
      continue;
    } else {
      return false;
    }
  }
  if (str[i] != '\0') {
    return false;
  }
  return true;
}

/**
 * Helper method of encode().
 *
 * Convert 3 input bytes (part of the binary representation) into 4 output bytes (part of the base64
 * ASCII representation) according to base64 encoding.
 */
void Base64::encode_3byte_block(const unsigned char *in, char *out) {
  uint32_t val = (in[0] << 16) |
                 (in[1] <<  8) |
                 (in[2]);
  out[0] = kEncodeMap[ val >> 18        ];
  out[1] = kEncodeMap[(val >> 12) & 0x3f];
  out[2] = kEncodeMap[(val >>  6) & 0x3f];
  out[3] = kEncodeMap[ val        & 0x3f];
}

/** Similar to the previous, but for 2 input bytes (2 byte of the binary -> 3 ASCII characters */
void Base64::encode_2byte_block(const unsigned char *in, char *out) {
  uint16_t val = (in[0] <<  8) | (in[1]);
  out[0] = kEncodeMap[(val >> 10) & 0x3f];
  out[1] = kEncodeMap[(val >>  4) & 0x3f];
  out[2] = kEncodeMap[(val <<  2) & 0x3f];
}

/** Similar to the previous, but for 1 input byte (1 byte of the binary -> 2 ASCII characters */
void Base64::encode_1byte_block(const unsigned char *in, char *out) {
  uint8_t val = in[0];
  out[0] = kEncodeMap[ val >> 2        ];
  out[1] = kEncodeMap[(val << 4) & 0x3f];
}

void Base64::encode(const unsigned char* in, char *out, int in_length) {
  if (in_length == 16) {
    encode_3byte_block(&in[ 0], out);
    encode_3byte_block(&in[ 3], out +  4);
    encode_3byte_block(&in[ 6], out +  8);
    encode_3byte_block(&in[ 9], out + 12);
    encode_3byte_block(&in[12], out + 16);
    encode_1byte_block(&in[15], out + 20);
    out[22] = '\0';
  } else if (in_length == 8) {
    encode_3byte_block(&in[0], out);
    encode_3byte_block(&in[3], out + 4);
    encode_2byte_block(&in[6], out + 8);
    out[11] = '\0';
  } else {
    // TODO(rbalint) support other lengths, maybe drop those hand-unrolled loops if
    // something is faster
    abort();
  }
}

}  /* namespace firebuild */
