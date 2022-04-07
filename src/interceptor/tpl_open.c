{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the vararg open() family.                             #}
{# (The non-vararg __open_2() variants are handled elsewhere.)        #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### if msg_add_fields is not defined
###   if vararg
###     set msg_add_fields = ["if (__OPEN_NEEDS_MODE(flags)) fbbcomm_builder_" + msg + "_set_mode(&ic_msg, mode);"]
###   else
###     set msg_add_fields = []
###   endif
###   if "dirfd" in sig_str
###     do msg_add_fields.append("BUILDER_MAYBE_SET_ABSOLUTE_CANONICAL(" + msg + ", dirfd, file);")
###   else
###     do msg_add_fields.append("BUILDER_SET_ABSOLUTE_CANONICAL(" + msg + ", file);")
###   endif
###   do msg_add_fields.append("fbbcomm_builder_" + msg + "_set_pre_open_sent(&ic_msg, pre_open_sent);")
### endif
### set after_lines = ["if (i_am_intercepting) clear_notify_on_read_write_state(ret);"]
### set send_ret_on_success=True
### set ack_condition = "success && !is_path_at_locations(file, &system_locations)"

### block before
{{ super() }}
###   if vararg
  mode_t mode = 0;
  if (__OPEN_NEEDS_MODE(flags)) {
    mode = va_arg(ap, mode_t);
  }
###   endif
  const int pre_open_sent = i_am_intercepting && maybe_send_pre_open(AT_FDCWD, file, flags);
### endblock before

### block call_orig
### if vararg
  ret = ic_orig_{{ func }}({{ names_str }}, mode);
### else
  ret = ic_orig_{{ func }}({{ names_str }});
### endif
### endblock call_orig
