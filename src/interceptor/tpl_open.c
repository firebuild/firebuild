{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the open() family.                                    #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### if msg_add_fields is not defined
###   if vararg
###     set msg_add_fields = ["if (__OPEN_NEEDS_MODE(flags)) fbbcomm_builder_" + msg + "_set_mode(&ic_msg, mode);"]
###   else
###     set msg_add_fields = []
###   endif
###   if "dirfd" in sig_str
###     do msg_add_fields.append("BUILDER_MAYBE_SET_ABSOLUTE_CANONICAL(" + msg + ", dirfd, pathname);")
###   else
###     do msg_add_fields.append("BUILDER_SET_ABSOLUTE_CANONICAL(" + msg + ", pathname);")
###   endif
###   do msg_add_fields.append("fbbcomm_builder_" + msg + "_set_pre_open_sent(&ic_msg, pre_open_sent);")
### endif
### set after_lines = ["if (i_am_intercepting && success) clear_notify_on_read_write_state(ret);"]
### set send_ret_on_success=True
### set ack_condition = "success && !is_path_at_locations(fbbcomm_builder_" + msg + "_get_pathname(&ic_msg), fbbcomm_builder_" + msg + "_get_pathname_len(&ic_msg), &system_locations) && !is_path_at_locations(fbbcomm_builder_" + msg + "_get_pathname(&ic_msg), fbbcomm_builder_" + msg + "_get_pathname_len(&ic_msg), &ignore_locations)"

### block before
{{ super() }}
###   if vararg
  mode_t mode = 0;
  if (__OPEN_NEEDS_MODE(flags)) {
    mode = va_arg(ap, mode_t);
  }
###   endif
###   if "dirfd" not in sig_str
  const int dirfd = AT_FDCWD;
###   endif
  const int pre_open_sent = i_am_intercepting && maybe_send_pre_open(dirfd, pathname, flags);
### endblock before

### block call_orig
### if vararg
  ret = IC_ORIG({{ func }})({{ names_str }}, mode);
### else
  ret = IC_ORIG({{ func }})({{ names_str }});
### endif
### endblock call_orig
