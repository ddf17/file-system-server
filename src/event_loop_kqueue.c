/**
 * macOS kqueue implementation of the socket event loop abstraction.
 */
#include "fss/event_loop.h"

#include <errno.h>
#include <stddef.h>
#include <sys/event.h>
#include <sys/time.h>

static int update_filter(int loop_fd, int fd, int filter, int flags, int ignore_missing) {
    struct kevent event;
    EV_SET(&event, fd, filter, flags, 0, 0, NULL);

    if (kevent(loop_fd, &event, 1, NULL, 0, NULL) == -1) {
        if (ignore_missing && errno == ENOENT) {
            return 0;
        }
        return -1;
    }

    return 0;
}

int event_loop_create(void) {
    return kqueue();
}

int event_loop_add(int loop_fd, int fd, int events) {
    if ((events & FSS_EVENT_READ) &&
        update_filter(loop_fd, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0) == -1) {
        return -1;
    }

    if ((events & FSS_EVENT_WRITE) &&
        update_filter(loop_fd, fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0) == -1) {
        return -1;
    }

    return 0;
}

int event_loop_modify(int loop_fd, int fd, int events) {
    if (update_filter(loop_fd, fd, EVFILT_READ,
                      (events & FSS_EVENT_READ) ? (EV_ADD | EV_ENABLE)
                                                : (EV_DELETE),
                      1) == -1) {
        return -1;
    }

    if (update_filter(loop_fd, fd, EVFILT_WRITE,
                      (events & FSS_EVENT_WRITE) ? (EV_ADD | EV_ENABLE)
                                                 : (EV_DELETE),
                      1) == -1) {
        return -1;
    }

    return 0;
}

int event_loop_remove(int loop_fd, int fd) {
    int read_result = update_filter(loop_fd, fd, EVFILT_READ, EV_DELETE, 1);
    int write_result = update_filter(loop_fd, fd, EVFILT_WRITE, EV_DELETE, 1);

    if (read_result == -1 && write_result == -1) {
        return -1;
    }
    return 0;
}

int event_loop_wait(int loop_fd, fss_event *events, int max_events) {
    struct kevent kqueue_events[max_events];
    int ready = kevent(loop_fd, NULL, 0, kqueue_events, max_events, NULL);

    for (int i = 0; i < ready; i++) {
        events[i].fd = (int)kqueue_events[i].ident;
        events[i].events = 0;

        if (kqueue_events[i].filter == EVFILT_READ) {
            events[i].events |= FSS_EVENT_READ;
        }
        if (kqueue_events[i].filter == EVFILT_WRITE) {
            events[i].events |= FSS_EVENT_WRITE;
        }
        if (kqueue_events[i].flags & EV_ERROR) {
            events[i].events |= FSS_EVENT_ERROR;
        }
    }

    return ready;
}
