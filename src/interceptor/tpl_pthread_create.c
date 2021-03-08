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
{# Template for pthread_create, inherited from marker_only.           #}
{# Insert another trace markers, telling the pid.                     #}
{# ------------------------------------------------------------------ #}
### extends "tpl_marker_only.c"

{% set msg = None %}
{% set global_lock = False %}

### block no_intercept
  i_am_intercepting = false;
  (void)i_am_intercepting;
### endblock no_intercept

### block call_orig
  /* Need to pass two pointers using one. Allocate room on the heap,
   * placing it on the stack might not live long enough.
   * Will be free()d in pthread_start_routine_wrapper(). */
  void **routine_and_arg = malloc(2 * sizeof(void *));
  routine_and_arg[0] = start_routine;
  routine_and_arg[1] = arg;
  ret = get_ic_orig_pthread_create()(thread, attr, pthread_start_routine_wrapper, routine_and_arg);
### endblock call_orig
