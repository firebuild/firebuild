{# ------------------------------------------------------------------ #}
{# Copyright (c) 2023 Firebuild Inc.                                  #}
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
{# Template for shm_open()                                            #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block before
{{ super() }}
###   if vararg
###     if target == "darwin"
  int mode = 0;
###     else
  mode_t mode = 0;
###     endif
  if (__OPEN_NEEDS_MODE(oflag)) {
###     if target == "darwin"
    mode = va_arg(ap, int);
###     else
    mode = va_arg(ap, mode_t);
###     endif
  }
###   endif
### endblock before

### block call_orig
### if vararg
  ret = {{ call_ic_orig_func }}({{ names_str }}, mode);
### else
  ret = {{ call_ic_orig_func }}({{ names_str }});
### endif
### endblock call_orig
