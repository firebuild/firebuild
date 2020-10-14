{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the __libc_start_main() function.                     #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block body
  /* Preparations: Initialize the interceptor */
  fb_ic_load();

  /* Get out of the way from others */
  thread_intercept_on = NULL;
  pthread_mutex_unlock(&ic_global_lock);
  thread_has_global_lock = false;

  /* Mark the end now */
  insert_end_marker("{{ func }}");

  /* Perform the call */
  ic_orig_{{ func }}(main, argc, ubp_av, init, fini, rtld_fini, stack_end);

  /* Should not be reached */
  assert(0 && "ic_orig_{{ func }} must not return");
  abort(); /* for NDEBUG */
### endblock body
