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
{# Template for functions seeking a file or querying the offset.      #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### if msg_skip_fields is not defined
###   set msg_skip_fields = []
### endif
### do msg_skip_fields.append("error_no")

{% set msg = "seek_in_inherited" %}
{# No locking around the seek(), to follow the pattern of tpl_{read,write}.c #}
{% set global_lock = 'never' %}

### block set_fields
  {{ super() }}
  fbbcomm_builder_{{ msg }}_set_modify_offset(&ic_msg, modify_offset);
### endblock set_fields

### block send_msg
  bool modify_offset = {{ modify_offset }};

  {# Acquire the lock if sending a message #}
  if (fd < 0 || fd >= IC_FD_STATES_SIZE ||
      (modify_offset == false && ic_fd_states[fd].notify_on_tell == true) ||
      (modify_offset == true && ic_fd_states[fd].notify_on_seek == true)) {
    /* Need to notify the supervisor */

    {{ grab_lock_if_needed('true') | indent(2) }}

    {{ super() | indent(2) }}

    if (fd >= 0 && fd < IC_FD_STATES_SIZE) {
      ic_fd_states[fd].notify_on_tell = false;
      if (modify_offset) {
        ic_fd_states[fd].notify_on_seek = false;
      }
    }

    {{ release_lock_if_needed() | indent(2) }}
  }
### endblock send_msg
