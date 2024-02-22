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
{# Template for the posix_spawn_file_actions_...() family.            #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### set init_or_destroy = func in ["posix_spawn_file_actions_init", "posix_spawn_file_actions_destroy"]
### block guard_connection_fd
{# Override the main template's corresponding block so that the       #}
{# connection fd is _not_ guarded here. This is because matching the  #}
{# raw fd number against the _current_ connection fd number is        #}
{# incorrect. By the time the actions we register here will be        #}
{# executed, the communication fd might have moved elsewhere due to   #}
{# an intercepted dup2(), or reopened as a regular file due to a      #}
{# preceding posix_spawn_file_action. See #875 for further details.   #}
### endblock

### block before
###   if not init_or_destroy
    const posix_spawn_file_actions_t file_actions_orig = *file_actions;
###   endif
### endblock before

### block after
  if (success) {
###   if not init_or_destroy
    psfa_update_actions(&file_actions_orig, file_actions);
###   endif
    {{ func | replace("posix_spawn_file_actions_", "psfa_") }} ({{ names_str }});
  }
### endblock after

### block send_msg
  /* No supervisor communication */
### endblock send_msg
