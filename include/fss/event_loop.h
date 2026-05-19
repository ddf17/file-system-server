/**
 * Cross-platform socket event loop abstraction.
 */
#pragma once

#define FSS_EVENT_READ  0x01
#define FSS_EVENT_WRITE 0x02
#define FSS_EVENT_ERROR 0x04

typedef struct {
    int fd;
    int events;
} fss_event;

int event_loop_create(void);
int event_loop_add(int loop_fd, int fd, int events);
int event_loop_modify(int loop_fd, int fd, int events);
int event_loop_remove(int loop_fd, int fd);
int event_loop_wait(int loop_fd, fss_event *events, int max_events);
