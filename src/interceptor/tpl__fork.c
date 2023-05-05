{# ------------------------------------------------------------------ #}
{# Copyright (c) 2022 Firebuild Inc.                                  #}
{# All rights reserved.                                               #}
{# Free for personal use and commercial trial.                        #}
{# Non-trial commercial use requires licenses available from          #}
{# https://firebuild.com.                                             #}
{# Modification and redistribution are permitted, but commercial use  #}
{# of derivative works is subject to the same requirements of this    #}
{# license.                                                           #}
{# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,    #}
{# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF #}
{# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND              #}
{# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT        #}
{# HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,       #}
{# WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, #}
{# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER      #}
{# DEALINGS IN THE SOFTWARE.                                          #}
{# ------------------------------------------------------------------ #}
{# Template for the _Fork() and vfork() calls.                        #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block call_orig
###   if func == 'vfork'
  /* vfork interception would be a bit complicated to implement properly
   * and most of the programs will work properly with _Fork */
###   endif
  ret = get_ic_orig__Fork()();
### endblock call_orig

### block after

  if (!success) {
    /* Error */
    // FIXME: disable shortcutting
  }
  /* In the child, what we need to do here is done via our atfork_child_handler().
   * In the parent there's nothing to do here at all. */
### endblock after

### block send_msg
  /* Notify the supervisor */
  if (!success) {
    /* Error, nothing here to do */
  } else if (ret == 0) {
  /* Make sure the child cannot receive a signal until it builds up
   * the new connection to the supervisor. To do this, we must block
   * signals before forking. */
  sigset_t set_orig, set_block_all;
  sigfillset(&set_block_all);
  ic_pthread_sigmask(SIG_SETMASK, &set_block_all, &set_orig);

  atfork_child_handler();

  ic_pthread_sigmask(SIG_SETMASK, &set_orig, NULL);
  } else {
    atfork_parent_handler();
  }

### endblock send_msg
