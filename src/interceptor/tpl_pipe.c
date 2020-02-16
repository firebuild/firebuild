{# ------------------------------------------------------------------ #}
{# Copyright (c) 2020 Interri Kft.                                    #}
{# This file is an unpublished work. All rights reserved.             #}
{# ------------------------------------------------------------------ #}
{# Template for the pipe() and pipe2() calls.                         #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

### block call_orig
  if (i_am_intercepting) {
    /* create fake pipe _without_ calling ic_orig_pipe() */

    /* create reasonably unique fifo */
    ic_orig_clock_gettime(CLOCK_REALTIME, &time);
    snprintf(fd0_fifo, sizeof(fd0_fifo), "%s-%d-0-%09ld-%09ld",
             getenv("FB_SOCKET"), getpid(), time.tv_sec, time.tv_nsec);
    snprintf(fd1_fifo, sizeof(fd1_fifo), "%s-%d-1-%09ld-%09ld",
             getenv("FB_SOCKET"), getpid(), time.tv_sec, time.tv_nsec);

    ret = ic_orig_mkfifo(fd0_fifo, 0666);
    if (ret == -1) {
      // TODO(rbalint) maybe continue without shortcutting being possible
      assert(ret == 0 && "mkfifo(fd0_fifo, 0666) failed");
    }

    ret = ic_orig_mkfifo(fd1_fifo, 0666);
    if (ret == -1) {
      // TODO(rbalint) maybe continue without shortcutting being possible
      assert(ret == 0 && "mkfifo(fd1_fifo, 0666) failed");
    }

    /* send fifos to supervisor to open them first there because opening in blocking
     mode blocks until the other end is opened, too (see fifo(7)) */
    pipefd[0] = ic_orig_open(fd0_fifo, (flags & ~O_ACCMODE) | O_RDONLY | O_NONBLOCK);
    assert(pipefd[0] != -1);
    /* open fd1 for reading just to not block in opening for writing */
    tmp_fd = ic_orig_open(fd1_fifo, O_RDONLY | O_NONBLOCK);
    pipefd[1] = ic_orig_open(fd1_fifo, (flags & ~O_ACCMODE) | O_WRONLY);
    assert(pipefd[1] != -1);

    /* set return value for success */
    // TODO(rbalint) report errors
    ret = 0;

  } else {
    /* just create the pipe */
    ret = ic_orig_pipe2(pipefd, flags);
  }

### endblock call_orig
