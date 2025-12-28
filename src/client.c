/**
 * nonstop_networking
 */
#include "fss/format.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "fss/common.h"

char **parse_args(int argc, char **argv);
verb check_args(char **args);
int connect_to_server(const char *host, const char *port);

void handle_get(int socket, const char *remote, const char *local);
void handle_put(int socket, const char *remote, const char *local);
void handle_delete(int socket, const char *remote);
void handle_list(int socket);


int main(int argc, char **argv) {
    // Good luck!
    char **args = parse_args(argc, argv);
    verb command = check_args(args);

    const char *host = args[0];
    const char *port = args[1];
    const char *remote_file = args[3];
    const char *local_file = args[4];

    int socket_fd = connect_to_server(host, port);
    if (socket_fd == -1) {
        free(args);
        exit(1);
    }

    switch (command) {
        case GET:
            handle_get(socket_fd, remote_file, local_file);
            break;
        case PUT:
            handle_put(socket_fd, remote_file, local_file);
            break;
        case DELETE:
            handle_delete(socket_fd, remote_file);
            break;
        case LIST:
            handle_list(socket_fd);
            break;
        default:
            fprintf(stderr, "Unknown command\n");
            break;
    }

    close(socket_fd);
    free(args);
    return 0;
}

int connect_to_server(const char *host, const char *port) {
    struct addrinfo hints, *res;
    int socket_fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0) {
        perror("getaddrinfo failed");
        return -1;
    }

    socket_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (socket_fd == -1) {
        perror("socket creation failed");
        freeaddrinfo(res);
        return -1;
    }

    if (connect(socket_fd, res->ai_addr, res->ai_addrlen) == -1) {
        perror("connect failed");
        close(socket_fd);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);
    return socket_fd;
}

void handle_get(int socket, const char *remote, const char *local) {
    char request_buffer[1024];
    int req_len = snprintf(request_buffer, sizeof(request_buffer), "GET %s\n", remote);
    write_all_to_socket(socket, request_buffer, req_len);

    shutdown(socket, SHUT_WR);

    char status_buffer[1024];
    ssize_t len = read_line_from_socket(socket, status_buffer, sizeof(status_buffer));

    if (len <= 0) {
        print_connection_closed();
        return;
    }

    if (strcmp(status_buffer, "ERROR\n") == 0) {
        char error_buffer[1024];
        ssize_t err_len = read_line_from_socket(socket, error_buffer, sizeof(error_buffer));
        if (err_len > 0) {
            if (error_buffer[err_len - 1] == '\n') {
                error_buffer[err_len - 1] = '\0';
            }
            print_error_message(error_buffer);
        } else {
            print_invalid_response();
        }
    } else if (strcmp(status_buffer, "OK\n") == 0) {
        // 1. Read the file size
        size_t file_size;
        ssize_t size_read = read_all_from_socket(socket, (char *)&file_size, sizeof(size_t));

        if (size_read != sizeof(size_t)) {
            print_too_little_data();
            return;
        }

        // 2. Open the local file 
        int local_fd = open(local, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (local_fd == -1) {
            perror("open local file failed");
            return;
        }

        // 3. Loop
        char file_buffer[4096];
        size_t total_bytes_received = 0;
        while (total_bytes_received < file_size) {
            size_t bytes_to_read = sizeof(file_buffer);
            if (file_size - total_bytes_received < bytes_to_read) {
                bytes_to_read = file_size - total_bytes_received;
            }

            ssize_t bytes_received = read_all_from_socket(socket, file_buffer, bytes_to_read);

            if (bytes_received <= 0) {
                print_too_little_data();
                close(local_fd);
                return;
            }

            write_all_to_socket(local_fd, file_buffer, bytes_received);
            total_bytes_received += bytes_received;
        }

        char extra_byte;
        ssize_t extra_read = read(socket, &extra_byte, 1);
        if (extra_read > 0) {
            print_received_too_much_data();
        }

        close(local_fd);

    } else {
        print_invalid_response();
    }
}

void handle_put(int socket, const char *remote, const char *local) {
    FILE *file = fopen(local, "r");
    if (file == NULL) {
        perror("Failed to open local file");
        return; 
    }
    size_t file_size = get_file_size(local);

    char request_buffer[1024];
    int header_len = snprintf(request_buffer, sizeof(request_buffer), "PUT %s\n", remote);
    write_all_to_socket(socket, request_buffer, header_len);

    write_all_to_socket(socket, (char *)&file_size, sizeof(size_t)); 

    char file_chunk[4096];
    size_t bytes_read;
    while ((bytes_read = fread(file_chunk, 1, sizeof(file_chunk), file)) > 0) {
        write_all_to_socket(socket, file_chunk, bytes_read);
    }
    fclose(file);

    shutdown(socket, SHUT_WR); 

    // This part is identical to handle_delete's response logic
    char status_buffer[1024];
    ssize_t len = read_line_from_socket(socket, status_buffer, sizeof(status_buffer));

    if (len <= 0) {
        print_connection_closed();
        return;
    }

    if (strcmp(status_buffer, "OK\n") == 0) {
        print_success();
    } else if (strcmp(status_buffer, "ERROR\n") == 0) {
        char error_buffer[1024];
        ssize_t err_len = read_line_from_socket(socket, error_buffer, sizeof(error_buffer));
        if (err_len > 0) {
            if (error_buffer[err_len - 1] == '\n') {
                error_buffer[err_len - 1] = '\0';
            }
            print_error_message(error_buffer);
        } else {
            print_invalid_response();
        }
    } else {
        print_invalid_response();
    }
}

void handle_delete(int socket, const char *remote) {
    char request_buffer[1024];
    int req_len = snprintf(request_buffer, sizeof(request_buffer), "DELETE %s\n", remote);
    write_all_to_socket(socket, request_buffer, req_len);

    shutdown(socket, SHUT_WR); 

    char status_buffer[1024];
    ssize_t len = read_line_from_socket(socket, status_buffer, sizeof(status_buffer));

    if (len <= 0) {
        print_connection_closed();
        return;
    }

    if (strcmp(status_buffer, "OK\n") == 0) {
        print_success(); 
    } else if (strcmp(status_buffer, "ERROR\n") == 0) {
        char error_buffer[1024];
        ssize_t err_len = read_line_from_socket(socket, error_buffer, sizeof(error_buffer));
        
        if (err_len > 0) {
            if (error_buffer[err_len - 1] == '\n') {
                error_buffer[err_len - 1] = '\0';
            }
            print_error_message(error_buffer);
        } else {
            print_invalid_response(); 
        }
    } else {
        print_invalid_response();
    }
}

void handle_list(int socket) {
    const char *request = "LIST\n"; 
    write_all_to_socket(socket, request, strlen(request));

    shutdown(socket, SHUT_WR); 

    char status_buffer[1024];
    ssize_t len = read_line_from_socket(socket, status_buffer, sizeof(status_buffer));

    if (len <= 0) {
        print_connection_closed();
        return;
    }

    if (strcmp(status_buffer, "ERROR\n") == 0) {
        char error_buffer[1024];
        ssize_t err_len = read_line_from_socket(socket, error_buffer, sizeof(error_buffer));
        if (err_len > 0) {
            if (error_buffer[err_len - 1] == '\n') {
                error_buffer[err_len - 1] = '\0';
            }
            print_error_message(error_buffer);
        } else {
            print_invalid_response();
        }
    } else if (strcmp(status_buffer, "OK\n") == 0) {
        size_t list_size;
        ssize_t size_read = read_all_from_socket(socket, (char *)&list_size, sizeof(size_t));

        if (size_read != sizeof(size_t)) {
            print_too_little_data();
            return;
        }

        if (list_size == 0) {
            return;
        }

        char *list_buffer = malloc(list_size);
        if (list_buffer == NULL) {
            perror("malloc failed");
            return;
        }

        ssize_t list_read = read_all_from_socket(socket, list_buffer, list_size);
        if (list_read < 0 || (size_t)list_read != list_size) {
            print_too_little_data();
            free(list_buffer);
            return;
        }
        write_all_to_socket(STDOUT_FILENO, list_buffer, list_size);

        char extra_byte;
        ssize_t extra_read = read(socket, &extra_byte, 1);
        if (extra_read > 0) {
            print_received_too_much_data();
        }

        free(list_buffer);

    } else {
        print_invalid_response();
    }
}

/**
 * Given commandline argc and argv, parses argv.
 *
 * argc argc from main()
 * argv argv from main()
 *
 * Returns char* array in form of {host, port, method, remote, local, NULL}
 * where `method` is ALL CAPS
 */
char **parse_args(int argc, char **argv) {
    if (argc < 3) {
        return NULL;
    }

    char *host = strtok(argv[1], ":");
    char *port = strtok(NULL, ":");
    if (port == NULL) {
        return NULL;
    }

    char **args = calloc(1, 6 * sizeof(char *));
    args[0] = host;
    args[1] = port;
    args[2] = argv[2];
    char *temp = args[2];
    while (*temp) {
        *temp = toupper((unsigned char)*temp);
        temp++;
    }
    if (argc > 3) {
        args[3] = argv[3];
    }
    if (argc > 4) {
        args[4] = argv[4];
    }

    return args;
}

/**
 * Validates args to program.  If `args` are not valid, help information for the
 * program is printed.
 *
 * args     arguments to parse
 *
 * Returns a verb which corresponds to the request method
 */
verb check_args(char **args) {
    if (args == NULL) {
        print_client_usage();
        exit(1);
    }

    char *command = args[2];

    if (strcmp(command, "LIST") == 0) {
        return LIST;
    }

    if (strcmp(command, "GET") == 0) {
        if (args[3] != NULL && args[4] != NULL) {
            return GET;
        }
        print_client_help();
        exit(1);
    }

    if (strcmp(command, "DELETE") == 0) {
        if (args[3] != NULL) {
            return DELETE;
        }
        print_client_help();
        exit(1);
    }

    if (strcmp(command, "PUT") == 0) {
        if (args[3] == NULL || args[4] == NULL) {
            print_client_help();
            exit(1);
        }
        return PUT;
    }

    // Not a valid Method
    print_client_help();
    exit(1);
}
