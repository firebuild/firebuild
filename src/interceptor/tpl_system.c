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
{# Template for the system() call.                                    #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block before
  {
    pthread_mutex_lock(&ic_system_popen_lock);
    /* Notify the supervisor before the call */
    FBBCOMM_Builder_system ic_msg;
    fbbcomm_builder_system_init(&ic_msg);
    fbbcomm_builder_system_set_cmd(&ic_msg, cmd);
    fb_fbbcomm_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
  }
### endblock before

### block call_orig
  ENVIRON_SAVE_AND_FIXUP(did_env_fixup, environ_saved);

  {{ super() }}

  ENVIRON_RESTORE(did_env_fixup, environ_saved);
### endblock call_orig

### block send_msg
  {
    /* Notify the supervisor after the call */
    FBBCOMM_Builder_system_ret ic_msg;
    fbbcomm_builder_system_ret_init(&ic_msg);
    fbbcomm_builder_system_ret_set_cmd(&ic_msg, cmd);
    fbbcomm_builder_system_ret_set_ret(&ic_msg, ret);
    fbbcomm_builder_system_ret_set_error_no(&ic_msg, saved_errno);
    fb_fbbcomm_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
    pthread_mutex_unlock(&ic_system_popen_lock);
  }
### endblock send_msg
