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
{# Template for functions writing to a (regular or special) file,     #}
{# including                                                          #}
{# - low-level [p]write*() family                                     #}
{# - high-level stdio like fwrite(), putc(), printf(), perror() etc.  #}
{# - low-level socket writing send*() family                          #}
{# - ftruncate()                                                      #}
{# and perhaps more.                                                  #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### if is_pwrite is not defined
###   set is_pwrite = "false"
### endif

### if msg_skip_fields is not defined
###   set msg_skip_fields = []
### endif
### do msg_skip_fields.append("error_no")

{% set msg = "write_to_inherited" %}
{# No locking around the write(): see issue #279 #}
{% set global_lock = 'never' %}

### block set_fields
  {{ super() }}
  fbbcomm_builder_{{ msg }}_set_is_pwrite(&ic_msg, is_pwrite);
### endblock set_fields

### block send_msg
  bool is_pwrite = {{ is_pwrite }};

  {# Acquire the lock if sending a message #}
  if (notify_on_write(fd, is_pwrite)) {
    /* Need to notify the supervisor */

    {{ grab_lock_if_needed('true') | indent(2) }}

    {{ super() | indent(2) }}

    set_notify_on_write_state(fd, is_pwrite);

    {{ release_lock_if_needed() | indent(2) }}
  }
### endblock send_msg
