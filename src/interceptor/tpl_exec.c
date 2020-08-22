{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the exec() family.                                    #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

{# Nasty hacks. Note that the func[4:] stuff also work with "fexecve". #}
{% set l = ('l' in func[4:]) %}
{% set v = ('v' in func[4:]) %} {# Exactly one of 'l' and 'v' is True. #}
{% set p = ('p' in func[4:]) %}
{% set e = ('e' in func[4:]) %}
{% set f = (func[0] == 'f') %}
{% set at = (func[-2:] == 'at') %}

### block body
###   if l
  /* Convert "arg, ..." to "argv[]" */
  unsigned int argc = 0, argc_size = 16;
  char **argv = static_cast<char **>(malloc(argc_size * sizeof(char*)));
  argv[argc] = const_cast<char *>(arg);
  while (argv[argc]) {
    argv[++argc] = static_cast<char *>(va_arg(ap, char*));
    if (argc == argc_size - 1) {
      argc_size *= 2;
      argv = static_cast<char **>(realloc(argv, argc_size * sizeof(char*)));
    }
  }
###     if e
  /* Also locate the environment */
  char **envp = static_cast<char **>(va_arg(ap, char**));
###     endif
###   endif

  {
    /* Notify the supervisor before the call */
    FBB_Builder_execv ic_msg;
    fbb_execv_init(&ic_msg);
###   if not f
    fbb_execv_set_file(&ic_msg, file);
###   else
    /* Set for fexec*() */
    fbb_execv_set_fd(&ic_msg, fd);
###   endif
###   if at
    /* Set for exec*at() */
    fbb_execv_set_dirfd(&ic_msg, dirfd);
    // fbb_execv_set_flags(&ic_msg, flags);
###   endif
###   if p
    /* Set for exec*p*() */
    fbb_execv_set_with_p(&ic_msg, true);
    char *path_env;
    size_t confstr_len = 0;
    if ((path_env = getenv("PATH"))) {
      fbb_execv_set_path(&ic_msg, path_env);
    } else {
      /* We have to fall back as described in man execvp.
       * This code is for glibc >= 2.24. For older versions
       * we'd need to prepend ".:", see issue 153. */
      confstr_len = ic_orig_confstr(_CS_PATH, NULL, 0);
    }
    /* Use the stack rather than the heap, make sure it lives
     * until we send the message. */
    char path_confstr[confstr_len];
    if (confstr_len > 0) {
      ic_orig_confstr(_CS_PATH, path_confstr, confstr_len);
      fbb_execv_set_path(&ic_msg, path_confstr);
    }
###   endif

    /* Command line arguments */
    fbb_execv_set_arg(&ic_msg, argv);

    /* Environment variables */
###   if e
    fbb_execv_set_env(&ic_msg, envp);
###   else
    fbb_execv_set_env(&ic_msg, environ);
###   endif

    /* Get CPU time used up to this exec() */
    struct rusage ru;
    ic_orig_getrusage(RUSAGE_SELF, &ru);
    fbb_execv_set_utime_u(&ic_msg,
        (int64_t)ru.ru_utime.tv_sec * 1000000 + (int64_t)ru.ru_utime.tv_usec);
    fbb_execv_set_stime_u(&ic_msg,
        (int64_t)ru.ru_stime.tv_sec * 1000000 + (int64_t)ru.ru_stime.tv_usec);

    fb_fbb_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
  }

  /* Perform the call. */
###   if l
  /* Instead of execl*(), call its execv*() counterpart. */
###   endif
  errno = saved_errno;
  ret = ic_orig_{{ func | replace("l", "v") }}({% if at %}dirfd, {% endif %}{% if f %}fd{% else %}file{% endif %}, argv{% if e %}, envp{% endif %}{% if at %}, flags{% endif %});
  saved_errno = errno;

### if l
  free(argv);
### endif

  {
    /* Notify the supervisor after the call */
    FBB_Builder_execv_failed ic_msg;
    fbb_execv_failed_init(&ic_msg);
    fbb_execv_failed_set_error_no(&ic_msg, saved_errno);
    fb_fbb_send_msg_and_check_ack(&ic_msg, fb_sv_conn);
  }
### endblock body
