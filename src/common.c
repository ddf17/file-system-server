/**
 * nonstop_networking
 */
#include "fss/common.h"
//pt1 finished

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

ssize_t read_all_from_socket(int socket, char *buffer, size_t count) {
    size_t total_read = 0;
    while (total_read < count) {
        ssize_t n = read(socket, buffer + total_read, count - total_read);
        if (n == -1) {
            if (errno == EINTR) continue;
            perror("read error");
            return -1;
        }
        if (n == 0) {
            return total_read;
        }
        total_read += n;
    }
    return total_read;
}

ssize_t write_all_to_socket(int socket, const char *buffer, size_t count) {
    size_t total_written = 0;
    while (total_written < count) {
        ssize_t n = write(socket, buffer + total_written, count - total_written);
        if (n == -1) {
            if (errno == EINTR) continue;
            perror("write error");
            return -1;
        }
        total_written += n;
    }
    return total_written;
}

ssize_t read_line_from_socket(int socket, char *buffer, size_t size) {
    size_t total_read = 0;
    while (total_read < size - 1) {
        char c;
        ssize_t n = read(socket, &c, 1);

        if (n == -1) {
            if (errno == EINTR) continue;
            perror("read_line read error");
            return -1;
        }
        if (n == 0) {
            if (total_read == 0) {
                return 0;
            } else {
                break;
            }
        }

        buffer[total_read] = c;
        total_read++;

        if (c == '\n') {
            break;
        }
    }
    buffer[total_read] = '\0';
    return total_read;
}

size_t get_file_size(const char *filename) {
    struct stat s;
    if (stat(filename, &s) == -1) {
        perror("stat error");
        return -1;
    }
    return s.st_size;
}
