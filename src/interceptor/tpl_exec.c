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
  va_list ap;
  unsigned int argc = 0, argc_size = 16;
  char **argv = static_cast<char **>(malloc(argc_size * sizeof(char*)));
  va_start(ap, arg);
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
  va_end(ap);
###   endif

  {
    /* Notify the supervisor before the call */
    msg::InterceptorMsg ic_msg;
    auto m = ic_msg.mutable_execv();
###   if not f
    if (file != NULL) m->set_file(file);
###   else
    /* Set for fexec*() */
    m->set_fd(fd);
###   endif
###   if at
    /* Set for exec*at() */
    m->set_dirfd(dirfd);
    // m->set_flags(flags);
###   endif
###   if p
    /* Set for exec*p*() */
    m->set_with_p(true);
    char *path;
    if ((path = getenv("PATH"))) {
      m->set_path(path);
    } else {
      /* We have to fall back as described in man execvp.
       * This code is for glibc >= 2.24. For older versions
       * we'd need to prepend ".:", see issue 153. */
      size_t n = ic_orig_confstr(_CS_PATH, NULL, 0);
      path = (char *)malloc(n);
      assert(path != NULL);
      ic_orig_confstr(_CS_PATH, path, n);
      m->set_path(path);
      free(path);
    }
###   endif

    /* Command line arguments */
    for (int i = 0; argv[i] != NULL; i++) {
      m->add_arg(argv[i]);
    }

    /* Environment variables */
###   if e
    for (int i = 0; envp[i] != NULL; i++) {
      m->add_env(envp[i]);
    }
###   else
    for (int i = 0; environ[i] != NULL; i++) {
      m->add_env(environ[i]);
    }
###   endif

    /* Get CPU time used up to this exec() */
    struct rusage ru;
    ic_orig_getrusage(RUSAGE_SELF, &ru);
    m->set_utime_u((int64_t)ru.ru_utime.tv_sec * 1000000 + (int64_t)ru.ru_utime.tv_usec);
    m->set_stime_u((int64_t)ru.ru_stime.tv_sec * 1000000 + (int64_t)ru.ru_stime.tv_usec);

    fb_send_msg_and_check_ack(ic_msg, fb_sv_conn);
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
    msg::InterceptorMsg ic_msg;
    auto m = ic_msg.mutable_execvfailed();
    m->set_error_no(saved_errno);
    fb_send_msg_and_check_ack(ic_msg, fb_sv_conn);
  }
### endblock body
