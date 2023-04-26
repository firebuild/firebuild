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
{# Template for functions reading from a (regular or special) file,   #}
{# including                                                          #}
{# - low-level [p]read*() family                                      #}
{# - high-level stdio like fread(), getc(), scanf() etc.              #}
{# - low-level socket reading recv*() family                          #}
{# and perhaps more.                                                  #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### if is_pread is not defined
###   set is_pread = "false"
### endif

### if msg_skip_fields is not defined
###   set msg_skip_fields = []
### endif
### do msg_skip_fields.append("error_no")

{% set msg = "read_from_inherited" %}
{# No locking around the read(): see issue #279 #}
{% set global_lock = 'never' %}

### block set_fields
  {{ super() }}
  fbbcomm_builder_{{ msg }}_set_is_pread(&ic_msg, is_pread);
### endblock set_fields

### block send_msg
  bool is_pread = {{ is_pread }};

  {# Acquire the lock if sending a message #}
  if (notify_on_read(fd, is_pread)) {
    /* Need to notify the supervisor */

    {{ grab_lock_if_needed('true') | indent(2) }}

    {{ super() | indent(2) }}

    set_notify_on_read_state(fd, is_pread);

    {{ release_lock_if_needed() | indent(2) }}
  }
### endblock send_msg
