{# ------------------------------------------------------------------ #}
{# Copyright (c) 2022 Firebuild Inc.                                  #}
{# All rights reserved.                                               #}
{# Free for personal use and commercial trial.                        #}
{# Non-trial commercial use requires licenses available from          #}
{# https://firebuild.com                                              #}
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

### block def_c
bool ic_called_{{ func }};
{{ super() }}
### endblock def_c

### block send_msg
  /* Notify the supervisor */
  if (!ic_called_{{ func }}) {
    ic_called_{{ func }} = true;
    FBBCOMM_Builder_{{ msg }} ic_msg;
    fbbcomm_builder_{{ msg }}_init(&ic_msg);
###   if msg == 'gen_call'
    fbbcomm_builder_{{ msg }}_set_call(&ic_msg, "{{ func }}");
###   endif

###   if ack
    /* Send and wait for ack */
    fb_fbbcomm_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
###   else
    /* Send and go on, no ack */
    fb_fbbcomm_send_msg(&ic_msg, fb_sv_conn);
###   endif
  }
### endblock send_msg
