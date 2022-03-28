/*
 * Copyright (c) 2022 Firebuild Inc.
 * All rights reserved.
 *
 * Free for personal use and commercial trial.
 * Non-trial commercial use requires licenses available from https://firebuild.com.
 * Modification and redistribution are permitted, but commercial use of derivative
 * works is subject to the same requirements of this license
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef FIREBUILD_EPOLL_H_
#define FIREBUILD_EPOLL_H_

#ifdef __APPLE__
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
// TODO(rbalint) this is hackish, we should rather have a wrapper class
#define epoll_event kevent
#define EPOLLIN 0x001
#define EPOLLOUT 0x004
#else
#include <sys/epoll.h>
#endif
#include <time.h>
#include <unistd.h>

#include <cassert>
#include <queue>
#include <vector>

#include "firebuild/utils.h"

namespace firebuild {

typedef struct fd_context_ {
  /* The callback to call for this fd, passing the struct epoll_event as returned by epoll_wait(),
   * as well callback_user_data. Non-null if and only if the given fd is added to epollfd. */
#ifdef SET_EV
  void (*callback)(const struct kevent* event, void *callback_user_data);
#else
  void (*callback)(const struct epoll_event* event, void *callback_user_data);
#endif
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
  Epoll() {
#ifdef __APPLE__
    main_fd_ = kqueue();
    if (main_fd_ == -1) {
      perror("kqueue");
      abort();
    }
#else
    main_fd_ = epoll_create1(EPOLL_CLOEXEC);
#endif
  }
  ~Epoll();

  /** Whether we've added an fd to epollfd (according to our own bookkeeping */
  bool is_added_fd(int fd);

  /**
   * Dup already added fd to an fd that's not added yet to epollfd.
   * Also close fd. */
  int remap_to_not_added_fd(int fd);

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

#ifdef __APPLE__
  static int event_fd(const struct kevent* event) {
    return event->ident;
  }
  static void set_event_fd(struct kevent* event, int fd) {
    event->ident = static_cast<uintptr_t>(fd);
  }
  static bool ready_for_read(const struct epoll_event* event) {
    return event->ident & EVFILT_READ && !(event->ident & EV_EOF);
  }
  static bool ready_for_write(const struct epoll_event* event) {
    return event->ident & EVFILT_WRITE && !(event->ident & EV_EOF);
  }
#else
  static int event_fd(const struct epoll_event* event) {
    return event->data.fd;
  }
  static void set_event_fd(struct epoll_event* event, int fd) {
    event->data.fd = fd;
  }
  static bool ready_for_read(const struct epoll_event* event) {
    return event->events & EPOLLIN;
  }
  static bool ready_for_write(const struct epoll_event* event) {
    return event->events & EPOLLOUT;
  }
#endif

  /** Wrapper around epoll_wait(). Places the result in events_ and event_count_. */
  void wait();

  /** Call the relevant callback for all the returned events in events_, and all the expired
   *  timers. */
  void process_all_events() {
    /* Loop through the file descriptors for which the close() were missed. */
    while (!closed_context_fds_.empty()) {
      int fd = closed_context_fds_.front();
      delete_closed_fd_context(fd);
      closed_context_fds_.pop();
      close(fd);
    }

    /* Loop through the file descriptors.
     * In case of a signal, event_count_ might be -1, but that's fine for this loop. */
    for (event_current_ = 0; event_current_ < event_count_; event_current_++) {
#ifdef __APPLE__
      const int16_t filter = events_[event_current_].filter;
      if (filter != EVFILT_READ && filter != EVFILT_WRITE) {
        continue;
      }
#endif
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

  /** Clean up context for a closed fd. */
  void delete_closed_fd_context(int fd);

  /* Our main epoll fd. */
  int main_fd_ = -1;

  /* For each fd, tells its current role in epollfd. The entry is "active" (part of epoll's set)
   * if and only if its callback is non-null. */
  std::vector<fd_context> fd_contexts_ {};

  /* Closed fds that still have context in fd_contexts_. Those contexts need to be cleared
   * up before using the fds again with epoll_ctl(). */
  std::queue<int> closed_context_fds_ {};
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
