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
{# Template for the closefrom() call.                                 #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block guard_connection_fd
  /* Skip our standard connection fd guarding. */
### endblock guard_connection_fd

### block call_orig
  /* Reset our file states for fds that will be closed. */
  if (i_am_intercepting) {
    int i;
    for (i = lowfd; i < IC_FD_STATES_SIZE; i++) {
      set_notify_on_read_write_state(i);
    }
  }

  if (lowfd > fb_sv_conn) {
    /* Just go ahead. */
    get_ic_orig_closefrom()(lowfd);
  } else if (lowfd == fb_sv_conn) {
    /* Need to skip the first fd. */
    get_ic_orig_closefrom()(lowfd + 1);
  } else {
    /* Need to leave a hole in the range. */
    get_ic_orig_close_range()(lowfd, fb_sv_conn - 1, 0);
    get_ic_orig_closefrom()(fb_sv_conn + 1);
  }
### endblock call_orig
