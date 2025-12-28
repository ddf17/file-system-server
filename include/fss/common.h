/**
 * nonstop_networking
 */
#pragma once
#include <stddef.h>
#include <sys/types.h>

#define LOG(...)                      \
    do {                              \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n");        \
    } while (0);

typedef enum { GET, PUT, DELETE, LIST, V_UNKNOWN } verb;

ssize_t read_all_from_socket(int socket, char *buffer, size_t count);
ssize_t write_all_to_socket(int socket, const char *buffer, size_t count);
ssize_t read_line_from_socket(int socket, char *buffer, size_t size);
size_t get_file_size(const char *filename);
