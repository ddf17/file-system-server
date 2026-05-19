/**
 * Linux epoll implementation of the socket event loop abstraction.
 */
#include "fss/event_loop.h"

#include <string.h>
#include <sys/epoll.h>

static int to_epoll_events(int events) {
    int epoll_events = EPOLLET;

    if (events & FSS_EVENT_READ) {
        epoll_events |= EPOLLIN;
    }
    if (events & FSS_EVENT_WRITE) {
        epoll_events |= EPOLLOUT;
    }

    return epoll_events;
}

int event_loop_create(void) {
    return epoll_create1(0);
}

int event_loop_add(int loop_fd, int fd, int events) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = to_epoll_events(events);
    event.data.fd = fd;
    return epoll_ctl(loop_fd, EPOLL_CTL_ADD, fd, &event);
}

int event_loop_modify(int loop_fd, int fd, int events) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = to_epoll_events(events);
    event.data.fd = fd;
    return epoll_ctl(loop_fd, EPOLL_CTL_MOD, fd, &event);
}

int event_loop_remove(int loop_fd, int fd) {
    return epoll_ctl(loop_fd, EPOLL_CTL_DEL, fd, NULL);
}

int event_loop_wait(int loop_fd, fss_event *events, int max_events) {
    struct epoll_event epoll_events[max_events];
    int ready = epoll_wait(loop_fd, epoll_events, max_events, -1);

    for (int i = 0; i < ready; i++) {
        events[i].fd = epoll_events[i].data.fd;
        events[i].events = 0;

        if (epoll_events[i].events & EPOLLIN) {
            events[i].events |= FSS_EVENT_READ;
        }
        if (epoll_events[i].events & EPOLLOUT) {
            events[i].events |= FSS_EVENT_WRITE;
        }
        if (epoll_events[i].events & (EPOLLERR | EPOLLHUP)) {
            events[i].events |= FSS_EVENT_ERROR;
        }
    }

    return ready;
}
