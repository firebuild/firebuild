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

#ifndef COMMON_CSTRING_VIEW_H_
#define COMMON_CSTRING_VIEW_H_

/**
 * A lightweight structure containing a pointer to a string and the string's length.
 *
 * Intended to be used in a way that the string is a plain C '\0'-terminated string,
 * the length value doesn't include the trailing zero.
 *
 * Could be replaced by std::cstring_view, had this proposal not been rejected:
 *  - http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1402r0.pdf
 *  - https://github.com/cplusplus/papers/issues/189
 */

typedef struct {
  const char *c_str;
  uint32_t length;
} cstring_view;

#endif  // COMMON_CSTRING_VIEW_H_
