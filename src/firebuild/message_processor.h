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

#ifndef FIREBUILD_MESSAGE_PROCESSOR_H_
#define FIREBUILD_MESSAGE_PROCESSOR_H_

#include "firebuild/execed_process.h"
#include "firebuild/epoll.h"

namespace firebuild {

/** Handles incoming FBB messages from the interceptor */
class MessageProcessor {
 public:
  static void accept_exec_child(ExecedProcess* proc, int fd_conn, int fd0_reopen = -1);
  static void ic_conn_readcb(const struct epoll_event* event, void *ctx);
};

}  /* namespace firebuild */
#endif  // FIREBUILD_MESSAGE_PROCESSOR_H_
