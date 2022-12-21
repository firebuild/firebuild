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
{# Template for the close_range() call.                               #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block guard_connection_fd
  /* Skip our standard connection fd guarding. */
### endblock guard_connection_fd

### block call_orig
  /* Reset our file states for fds that will be closed. */
  if (i_am_intercepting && !(flags & CLOSE_RANGE_CLOEXEC)) {
    unsigned int i;
    for (i = first; i <= last && i < IC_FD_STATES_SIZE; i++) {
      set_notify_on_read_write_state(i);
    }
  }

  const unsigned int u_fb_sv_conn = (unsigned int)fb_sv_conn;
  if (first > u_fb_sv_conn || last < u_fb_sv_conn) {
    /* Just go ahead. */
    ret = ic_orig_close_range(first, last, flags);
  } else if (first == u_fb_sv_conn && last == u_fb_sv_conn) {
    /* Wishing to close only fb_sv_conn. Just pretend it succeeded. */
    ret = 0;
  } else if (first == u_fb_sv_conn) {
    /* Need to skip the first fd. */
    ret = ic_orig_close_range(first + 1, last, flags);
  } else if (last == u_fb_sv_conn) {
    /* Need to skip the last fd. */
    ret = ic_orig_close_range(first, last - 1, flags);
  } else {
    /* Need to leave a hole in the range. */
    int ret1 = ic_orig_close_range(first, u_fb_sv_conn - 1, 0);
    int ret2 = ic_orig_close_range(u_fb_sv_conn + 1, last, 0);
    ret = (ret1 == 0 && ret2 == 0) ? 0 : -1;
  }
### endblock call_orig
