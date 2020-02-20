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

  /* See comment in firebuild_fake_main() */
  char *main_and_argv[2];
  main_and_argv[0] = reinterpret_cast<char *>(main);
  main_and_argv[1] = reinterpret_cast<char *>(ubp_av);

  /* Get out of the way from others */
  intercept_on = NULL;

  /* Mark the end now */
  insert_end_marker("{{ func }}");

  /* Perform the call */
  ic_orig_{{ func }}(firebuild_fake_main, argc, main_and_argv, init, fini, rtld_fini, stack_end);

  /* Should not be reached */
  assert(0 && "fake_main must not return");
### endblock body
