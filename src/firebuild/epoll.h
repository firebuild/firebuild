/* Copyright (c) 2021 Interri Kft. */
/* This file is an unpublished work. All rights reserved. */

#ifndef FIREBUILD_EPOLL_H_
#define FIREBUILD_EPOLL_H_

#include <sys/epoll.h>
#include <time.h>
#include <unistd.h>

#include <cassert>
#include <vector>

#include "firebuild/utils.h"

namespace firebuild {

typedef struct fd_context_ {
  /* The callback to call for this fd, passing the struct epoll_event as returned by epoll_wait(),
   * as well callback_user_data. Non-null if and only if the given fd is added to epollfd. */
  void (*callback)(const struct epoll_event* event, void *callback_user_data);
  /* User data, as usual for callbacks. */
  void *callback_user_data;
} fd_context;

typedef struct timer_context_ {
  /* The callback to call for this timer, passing callback_user_data. Non-null if and only if the
   * given timer exists. */
  void (*callback)(void *callback_user_data);
  /* User data, as usual for callbacks. */
  void *callback_user_data;
  /* When to fire this, according to CLOCK_MONOTONIC. */
  struct timespec when;
} timer_context;

class Epoll {
 public:
  explicit Epoll(int flags) {
    main_fd_ = epoll_create1(flags);
  }
  ~Epoll() {
    close(main_fd_);
    for (size_t fd = 0; fd < fd_contexts_.size(); fd++) {
      if (fd_contexts_[fd].callback != nullptr) {
        /* This fd is still open while firebuild is quitting. This may be connected to a
         * orphan process. Simulate the termination of the process by closing the fd and letting
         * the callback to act on it and free the user data. */
        close(fd);
        if (fd_contexts_[fd].callback_user_data) {
          struct epoll_event fake_event;
          set_event_fd(&fake_event, fd);
          (*fd_contexts_[fd].callback)(&fake_event, fd_contexts_[fd].callback_user_data);
        }
      }
    }
  }

  /** Whether we've added an fd to epollfd (according to our own bookkeeping */
  bool is_added_fd(int fd);

  /** Thin wrapper around epoll_ctl(). Makes sure that the fd isn't added yet to epollfd
   *  (according to our own bookkeeping) and adds it with the given parameters. */
  void add_fd(int fd, uint32_t events,
              void (*callback)(const struct epoll_event* event, void *callback_user_data),
              void *callback_user_data);

  /** Thin wrapper around epoll_ctl(). Makes sure that the fd is already added to epollfd
   *  (according to our own bookkeeping) and removes it. */
  void del_fd(int fd);

  /** Thin wrapper around epoll_ctl(). Checks if fd is already added to epollfd
   *  (according to our own bookkeeping) and if so then removes it. */
  void maybe_del_fd(int fd);

  /** Add a one-shot timer, return its id */
  int add_timer(int ms,
                void (*callback)(void *callback_user_data),
                void *callback_user_data);

  /** Delete a one-shot timer by its id, before it fires. Make sure NOT to call this after the timer
   *  has fired! Don't even call it from the timer's own callback, the timer will clean up itself
   *  automatically. */
  void del_timer(int timer_id);

  static int event_fd(const struct epoll_event* event) {
    return event->data.fd;
  }
  static void set_event_fd(struct epoll_event* event, int fd) {
    event->data.fd = fd;
  }

  /** Wrapper around epoll_wait(). Places the result in events_ and event_count_. */
  inline void wait() {
    int timeout_ms = -1;
    if (next_timer_ >= 0) {
      struct timespec now, diff;
      clock_gettime(CLOCK_MONOTONIC, &now);
      if (timespeccmp(&timer_contexts_[next_timer_].when, &now, <)) {
        /* The next timer should already fire, calling timespecsub() would give a negative diff.
           Save the epoll_wait() system call and return immediately to process the timers. */
        event_count_ = 0;
        return;
      }
      timespecsub(&timer_contexts_[next_timer_].when, &now, &diff);
      timeout_ms = diff.tv_sec * 1000 + diff.tv_nsec / (1000 * 1000);
    }
    event_count_ = epoll_wait(main_fd_, events_, sizeof(events_) / sizeof(events_[0]), timeout_ms);
  }

  /** Call the relevant callback for all the returned events in events_, and all the expired
   *  timers. */
  void process_all_events() {
    /* Loop through the file descriptors.
     * In case of a signal, event_count_ might be -1, but that's fine for this loop. */
    for (event_current_ = 0; event_current_ < event_count_; event_current_++) {
      int fd = event_fd(&events_[event_current_]);
      /* fd might be -1, see in del_fd() for explanation. Skip those.
       * For the rest, call the appropriate callback. */
      if (fd >= 0) {
        assert(fd_contexts_[fd].callback != nullptr);
        (*fd_contexts_[fd].callback)(&events_[event_current_],
                                     fd_contexts_[fd].callback_user_data);
      }
    }

    /* Loop through the timers. Fire the elapsed ones in no particular order. */
    if (largest_timer_id_ >= 0) {
      struct timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      for (int i = 0; i <= largest_timer_id_; i++) {
        /* Skip the inactive entries (callback is null). */
        if (timer_contexts_[i].callback != nullptr &&
            timespeccmp(&timer_contexts_[i].when, &now, <)) {
          (*timer_contexts_[i].callback)(timer_contexts_[i].callback_user_data);
          del_timer(i);
        }
      }
    }
  }

 private:
  /** Make sure epoll_fd_contexts is large enough to contain fd. */
  inline void ensure_room_fd(int fd);

  /* Our main epoll fd. */
  int main_fd_ = -1;

  /* For each fd, tells its current role in epollfd. The entry is "active" (part of epoll's set)
   * if and only if its callback is non-null. */
  std::vector<fd_context> fd_contexts_ {};

  /* For each timer id, tells when to fire and what to call. The entry is "active" if and only its
   * callback is non-null. */
  std::vector<timer_context> timer_contexts_ {};
  /* Index to the last active item in timer_contexts_, or -1. */
  int largest_timer_id_ = -1;
  /* Index to the timer that will fire next, or -1. */
  int next_timer_ = -1;

  /* The place where epoll_wait() store the returned events. */
  struct epoll_event events_[32];
  /* The return value of epoll_wait(), i.e. the number of events placed in events_, or -1 */
  int event_count_ = 0;
  /* The index to the event in events_ we're currently processing. */
  int event_current_ = 0;
};

/* singleton */
extern Epoll *epoll;

}  /* namespace firebuild */

#endif  // FIREBUILD_EPOLL_H_
