{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for methods where we only need to notify the supervisor   #}
{# once per such method.                                              #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block decl_h
extern bool ic_called_{{ func }};
{{ super() }}
### endblock decl_h

### block reset_c
ic_called_{{ func }} = false;
### endblock reset_c

### block impl_c
bool ic_called_{{ func }};
{{ super() }}
### endblock impl_c

### block send_msg
  /* Notify the supervisor */
  if (!ic_called_{{ func }}) {
    ic_called_{{ func }} = true;
    FBBCOMM_Builder_gen_call ic_msg;
    fbbcomm_builder_gen_call_init(&ic_msg);
    fbbcomm_builder_gen_call_set_call(&ic_msg, "{{ func }}");

###   if ack
    /* Send and wait for ack */
    fb_fbbcomm_send_msg_and_check_ack_shmq(&ic_msg);
###   else
    /* Send and go on, no ack */
    fb_fbbcomm_send_msg_shmq(&ic_msg);
###   endif
  }
### endblock send_msg
