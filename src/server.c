/**
* nonstop_networking
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>

#include "fss/common.h"
#include "fss/format.h"
#include "fss/vector.h"
#include "dictionary.h"

#define MAX_EVENTS 64
#define MAX_HEADER_LEN 1024
#define BUFFER_SIZE 4096

static volatile sig_atomic_t running = 1;
static char *temp_dir = NULL;
static vector *server_files = NULL;
static dictionary *fd_to_client = NULL;
static int epoll_fd = -1;
static int listen_fd = -1;
//struct for proces need2 ??
typedef enum {
    STATE_RECV_HEADER, 
    STATE_RECV_PUT_SIZE,
    STATE_RECV_PUT_DATA,
    STATE_SEND_OK,
    STATE_SEND_ERROR,
    STATE_SEND_GET_SIZE,
    STATE_SEND_GET_DATA,
    STATE_SEND_LIST_DATA,
    STATE_DONE
} conn_state_t;

typedef struct {
    int fd;
    conn_state_t state;
    verb request_verb;

    char header_buf[MAX_HEADER_LEN];
    size_t header_len;

    char filename[256];

    int file_fd;
    size_t expected_size;
    size_t file_size;
    size_t bytes_transferred;

    char *list_buf;
    size_t list_len;
    size_t list_sent;
    char simple_response_buf[MAX_HEADER_LEN];
    size_t simple_response_len;
    size_t simple_response_sent;
} client_t;

static void recv_put_data(int epfd, client_t *client);
static void prepare_and_send_response(int epfd, client_t *client, const char *status, const char *msg);
static void start_send_list(int epfd, client_t *client);

static void signal_handler(int sig) {
    if (sig == SIGINT) running = 0;
}

static void install_sigint_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
}

static void cleanup_temp_directory(void) {
    if (!temp_dir) return;
    DIR *d = opendir(temp_dir);
    if (d) {
        struct dirent *dir;
        while ((dir = readdir(d)) != NULL) {
            if (strcmp(dir->d_name, ".") && strcmp(dir->d_name, "..")) {
                char filepath[1024];
                snprintf(filepath, sizeof(filepath), "%s/%s", temp_dir, dir->d_name);
                unlink(filepath);
            }
        }
        closedir(d);
    }
    rmdir(temp_dir);
}

static void cleanup(void) {
    cleanup_temp_directory();
    if (listen_fd != -1) close(listen_fd);
    if (epoll_fd != -1) close(epoll_fd);
    if (server_files) {
        for (size_t i = 0; i < vector_size(server_files); i++) {
            free(vector_get(server_files, i));
        }
        vector_destroy(server_files);
    }
    if (fd_to_client) dictionary_destroy(fd_to_client);
}

static void build_temp_directory(void) {
    static char temp_template[] = "XXXXXX";
    temp_dir = mkdtemp(temp_template);
    if (!temp_dir) {
        perror("mkdtemp failed");
        exit(1);  
    }
    print_temp_directory(temp_dir);
}


static int make_socket_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) return -1;
    return 0;
}

static int create_and_bind_socket(const char *port_str) {
    struct addrinfo hints, *res;
    int socket_fd;
    int reuse = 1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, port_str, &hints, &res) != 0) return -1;

    socket_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (socket_fd == -1) {
        freeaddrinfo(res);
        return -1;
    }
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

    if (bind(socket_fd, res->ai_addr, res->ai_addrlen) == -1) {
        close(socket_fd);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);

    if (listen(socket_fd, SOMAXCONN) == -1) {
        close(socket_fd);
        return -1;
    }

    make_socket_nonblocking(socket_fd);
    return socket_fd;
}

static void add_fd_to_epoll(int epfd, int fd, int events) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = events | EPOLLET;
    event.data.fd = fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
}

static void modify_epoll(int epfd, int fd, int events) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = events | EPOLLET;
    event.data.fd = fd;
    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &event);
}

/* client  */
static client_t *client_create(int fd) {
    client_t *client = calloc(1, sizeof(client_t));
    client->fd = fd;
    client->state = STATE_RECV_HEADER;
    client->file_fd = -1;
    return client;
}

static void client_destroy(client_t *client) {
    if (!client) return;
    if (client->file_fd != -1) close(client->file_fd);
    if (client->list_buf) free(client->list_buf);
    free(client);
}

static void close_and_cleanup(int epfd, client_t *client) {
    if (!client) return;

    epoll_ctl(epfd, EPOLL_CTL_DEL, client->fd, NULL);
    close(client->fd);

    if (client->request_verb == PUT && client->state != STATE_DONE) {
        char filepath[1024];
        snprintf(filepath, sizeof(filepath), "%s/%s", temp_dir, client->filename);
        unlink(filepath);
    }

    dictionary_remove(fd_to_client, &(client->fd));
    client_destroy(client);
}

//list

static char *build_list_buffer(size_t *out_len) {
    size_t total_data_size = 0;
    size_t count = vector_size(server_files);

    for (size_t i = 0; i < count; i++) {
        const char *filename = (const char *)vector_get(server_files, i);
        total_data_size += strlen(filename);
        if (i < count - 1) total_data_size += 1; // '\n'
    }

    size_t total_buffer_size = sizeof(size_t) + total_data_size;
    char *buffer = calloc(1, total_buffer_size);
    if (!buffer) return NULL;

    size_t net_list_size = total_data_size;
    memcpy(buffer, &net_list_size, sizeof(size_t));

    size_t offset = sizeof(size_t);
    for (size_t i = 0; i < count; i++) {
        const char *filename = (const char *)vector_get(server_files, i);
        size_t len = strlen(filename);
        memcpy(buffer + offset, filename, len);
        offset += len;
        if (i < count - 1) buffer[offset++] = '\n';
    }

    *out_len = total_buffer_size;
    return buffer;
}

static void add_file_to_list(const char *name) {
    for (size_t i = 0; i < vector_size(server_files); i++) {
        if (strcmp((const char *)vector_get(server_files, i), name) == 0) return;
    }
    vector_push_back(server_files, (void *)strdup(name));
}

static void remove_file_from_list(const char *name) {
    for (size_t i = 0; i < vector_size(server_files); i++) {
        if (strcmp((const char *)vector_get(server_files, i), name) == 0) {
            free(vector_get(server_files, i));
            vector_erase(server_files, i);
            return;
        }
    }
}

/* PUT */

static void recv_put_size(int epfd, client_t *client) {
    size_t size_len = sizeof(size_t);
    while (client->bytes_transferred < size_len) {
        size_t remaining = size_len - client->bytes_transferred;
        ssize_t n = read(client->fd,
                         ((char *)&client->expected_size) + client->bytes_transferred,
                         remaining);

        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            prepare_and_send_response(epfd, client, "ERROR", err_bad_file_size);
            return;
        }
        if (n == 0) {
            prepare_and_send_response(epfd, client, "ERROR", err_bad_file_size);
            return;
        }

        client->bytes_transferred += (size_t)n;
    }
    client->bytes_transferred = 0;

    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/%s", temp_dir, client->filename);
    client->file_fd = open(filepath,
                           O_WRONLY | O_CREAT | O_TRUNC,
                           S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (client->file_fd == -1) {
        prepare_and_send_response(epfd, client, "ERROR", err_bad_file_size);
        return;
    }

    client->state = STATE_RECV_PUT_DATA;
    recv_put_data(epfd, client);
}

// reusing a buffer instead of loading everything
static void recv_put_data(int epfd, client_t *client) {
    char buffer[BUFFER_SIZE];

    while (client->bytes_transferred < client->expected_size) {
        size_t bytes_to_read = BUFFER_SIZE;
        if (client->expected_size - client->bytes_transferred < bytes_to_read) {
            bytes_to_read = client->expected_size - client->bytes_transferred;
        }

        ssize_t n_read = read(client->fd, buffer, bytes_to_read);

        if (n_read == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            close(client->file_fd);
            client->file_fd = -1;
            prepare_and_send_response(epfd, client, "ERROR", err_bad_file_size);
            return;
        }

        if (n_read == 0) {
            close(client->file_fd);
            client->file_fd = -1;
            prepare_and_send_response(epfd, client, "ERROR", err_bad_file_size);
            return;
        }

        ssize_t n_written = pwrite(client->file_fd, buffer, (size_t)n_read, client->bytes_transferred);

        if (n_written != n_read) {
            close(client->file_fd);
            client->file_fd = -1;
            prepare_and_send_response(epfd, client, "ERROR", err_bad_file_size);
            return;
        }

        client->bytes_transferred += (size_t)n_read;
    }

    if (client->bytes_transferred == client->expected_size) {
        close(client->file_fd);
        client->file_fd = -1;

        char extra_byte;
        ssize_t extra_read = read(client->fd, &extra_byte, 1);
        if (extra_read > 0) {
            prepare_and_send_response(epfd, client, "ERROR", err_bad_file_size);
        } else {
            add_file_to_list(client->filename);
            prepare_and_send_response(epfd, client, "OK", NULL);
        }
    }
}
//GET
static void start_send_get(int epfd, client_t *client) {
    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/%s", temp_dir, client->filename);

    client->file_fd = open(filepath, O_RDONLY);
    if (client->file_fd == -1) {
        prepare_and_send_response(epfd, client, "ERROR", err_no_such_file);
        return;
    }

    struct stat st;
    if (fstat(client->file_fd, &st) == -1) {
        close(client->file_fd);
        client->file_fd = -1;
        prepare_and_send_response(epfd, client, "ERROR", err_no_such_file);
        return;
    }    
    client->file_size = (size_t)st.st_size;
    client->bytes_transferred = 0;

    prepare_and_send_response(epfd, client, "OK", NULL);
}

static void send_get_size(int epfd, client_t *client) {
    size_t size_len = sizeof(size_t);

    if (client->simple_response_len == 0) {
        memcpy(client->simple_response_buf, &client->file_size, size_len);
        client->simple_response_len = size_len;
        client->simple_response_sent = 0;
    }

    size_t bytes_left = client->simple_response_len - client->simple_response_sent;

    ssize_t n = write(client->fd,
                    client->simple_response_buf + client->simple_response_sent,
                    bytes_left);

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        close_and_cleanup(epfd, client);
        return;
    }

    client->simple_response_sent += (size_t)n;

    if (client->simple_response_sent == client->simple_response_len) {
        client->state = STATE_SEND_GET_DATA;
        client->simple_response_len = 0;
        client->simple_response_sent = 0;
        modify_epoll(epfd, client->fd, EPOLLOUT);
    }
}
//read from a specific offset 
static void send_get_data(int epfd, client_t *client) {
    char buffer[BUFFER_SIZE];

    while (client->bytes_transferred < client->file_size) {
        size_t read_len = BUFFER_SIZE;
        if (client->file_size - client->bytes_transferred < read_len)
            read_len = client->file_size - client->bytes_transferred;

        off_t offset = (off_t)client->bytes_transferred;
        ssize_t n_read = pread(client->file_fd, buffer, read_len, offset);

        if (n_read <= 0) {
            close_and_cleanup(epfd, client);
            return;
        }

        ssize_t n_written = write(client->fd, buffer, (size_t)n_read);

        if (n_written < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            close_and_cleanup(epfd, client);
            return;
        }

        client->bytes_transferred += (size_t)n_written;

        if (n_written < n_read) return;
    }

    if (client->bytes_transferred == client->file_size) {
        client->state = STATE_DONE;
        close_and_cleanup(epfd, client);
    }
}

/* DELETE */

static void delete_request(int epfd, client_t *client) {
    size_t index = (size_t)-1;
    for (size_t i = 0; i < vector_size(server_files); i++) {
        if (strcmp((const char *)vector_get(server_files, i), client->filename) == 0) {
            index = i;
            break;
        }
    }

    if (index == (size_t)-1) {
        prepare_and_send_response(epfd, client, "ERROR", err_no_such_file);
        return;
    }

    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/%s", temp_dir, client->filename);

    if (unlink(filepath) == -1) {
        prepare_and_send_response(epfd, client, "ERROR", err_no_such_file);
        return;
    }    

    remove_file_from_list(client->filename);
    prepare_and_send_response(epfd, client, "OK", NULL);
}

/* LIST */

static void send_list_data(int epfd, client_t *client) {
    size_t bytes_left = client->list_len - client->list_sent;

    ssize_t n = write(client->fd, client->list_buf + client->list_sent, bytes_left);

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        close_and_cleanup(epfd, client);
        return;
    }

    client->list_sent += (size_t)n;

    if (client->list_sent == client->list_len) {
        client->state = STATE_DONE;
        close_and_cleanup(epfd, client);
    }
}

static void execute_simple_response_write(int epfd, client_t *client) {
    size_t bytes_left = client->simple_response_len - client->simple_response_sent;

    ssize_t n = write(client->fd,
                    client->simple_response_buf + client->simple_response_sent,
                    bytes_left);

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        close_and_cleanup(epfd, client);
        return;
    }

    client->simple_response_sent += (size_t)n;

    if (client->simple_response_sent == client->simple_response_len) {
        if (client->state == STATE_SEND_OK) {
            if (client->request_verb == LIST) {
                start_send_list(epfd, client);
            } else if (client->request_verb == GET) {
                client->simple_response_len = 0;
                client->simple_response_sent = 0;
                client->state = STATE_SEND_GET_SIZE;
                modify_epoll(epfd, client->fd, EPOLLOUT);
            } else {
                client->state = STATE_DONE;
                close_and_cleanup(epfd, client);
            }
        } else if (client->state == STATE_SEND_ERROR) {
            client->state = STATE_DONE;
            close_and_cleanup(epfd, client);
        }
    } else {
        modify_epoll(epfd, client->fd, EPOLLOUT);
    }
}

static void prepare_and_send_response(int epfd, client_t *client, const char *status, const char *msg) {
    client->simple_response_sent = 0;

    if (strcmp(status, "OK") == 0) {
        client->simple_response_len = snprintf(client->simple_response_buf, MAX_HEADER_LEN, "OK\n");
        client->state = STATE_SEND_OK;
    } else {
        client->simple_response_len = snprintf(
            client->simple_response_buf, MAX_HEADER_LEN, "ERROR\n%s\n", msg ? msg : ""
        );
        client->state = STATE_SEND_ERROR;
    }

    if (client->simple_response_len >= MAX_HEADER_LEN)
        client->simple_response_len = MAX_HEADER_LEN - 1;

    execute_simple_response_write(epfd, client);
}

static void start_send_list(int epfd, client_t *client) {
    client->list_buf = build_list_buffer(&client->list_len);
    if (!client->list_buf) {
        prepare_and_send_response(epfd, client, "ERROR", err_bad_request);
        return;
    }
    client->list_sent = 0;
    client->state = STATE_SEND_LIST_DATA;
    modify_epoll(epfd, client->fd, EPOLLOUT);
}

static void parse_header_and_transition(int epfd, client_t *client) {
    char *newline = strchr(client->header_buf, '\n');
    if (!newline) return;

    *newline = '\0';

    char *verb_str = client->header_buf;
    char *filename = NULL;
    char *space = strchr(verb_str, ' ');

    if (space) {
        *space = '\0';
        filename = space + 1;
    }

    if (!strcmp(verb_str, "LIST")) {
        client->request_verb = LIST;
        if (filename) {
            prepare_and_send_response(epfd, client, "ERROR", err_bad_request);
        } else {
            prepare_and_send_response(epfd, client, "OK", NULL);
        }
    } else if (!strcmp(verb_str, "DELETE")) {
        client->request_verb = DELETE;
        if (!filename) {
            prepare_and_send_response(epfd, client, "ERROR", err_bad_request);
        } else {
            strncpy(client->filename, filename, sizeof(client->filename) - 1);
            delete_request(epfd, client);
        }
    } else if (!strcmp(verb_str, "GET")) {
        client->request_verb = GET;
        if (!filename) {
            prepare_and_send_response(epfd, client, "ERROR", err_bad_request);
        } else {
            strncpy(client->filename, filename, sizeof(client->filename) - 1);
            start_send_get(epfd, client);
        }
    } else if (!strcmp(verb_str, "PUT")) {
        client->request_verb = PUT;
        if (!filename) {
            prepare_and_send_response(epfd, client, "ERROR", err_bad_request);
        } else {
            strncpy(client->filename, filename, sizeof(client->filename) - 1);
            client->bytes_transferred = 0;
            client->state = STATE_RECV_PUT_SIZE;
            modify_epoll(epfd, client->fd, EPOLLIN);
        }
    } else {
        prepare_and_send_response(epfd, client, "ERROR", err_bad_request);
    }
}
// EPOLLIN ....
// reads the request header or reads file data for a PUT request
static void handle_read_event(int epfd, client_t *client) {
    if (client->state == STATE_RECV_HEADER) {
        while (client->header_len < MAX_HEADER_LEN - 1) {
            char c;
            ssize_t n = read(client->fd, &c, 1);

            if (n == -1) {
                if (errno == EINTR)
                    continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                close_and_cleanup(epfd, client);
                return;
            }

            if (n == 0) {
                close_and_cleanup(epfd, client);
                return;
            }

            client->header_buf[client->header_len++] = c;

            if (c == '\n') {
                client->header_buf[client->header_len] = '\0';
                parse_header_and_transition(epfd, client);
                return;
            }
        }

        if (client->header_len >= MAX_HEADER_LEN - 1) {
            prepare_and_send_response(epfd, client, "ERROR", err_bad_request);
        }
        return;

    } else if (client->state == STATE_RECV_PUT_SIZE) {
        recv_put_size(epfd, client);
    } else if (client->state == STATE_RECV_PUT_DATA) {
        recv_put_data(epfd, client);
    }
}


static void handle_write_event(int epfd, client_t *client) {
    if (client->state == STATE_SEND_OK || client->state == STATE_SEND_ERROR) {
        execute_simple_response_write(epfd, client);
    } else if (client->state == STATE_SEND_LIST_DATA) {
        send_list_data(epfd, client);
    } else if (client->state == STATE_SEND_GET_SIZE) {
        send_get_size(epfd, client);
    } else if (client->state == STATE_SEND_GET_DATA) {
        send_get_data(epfd, client);
    }
}

static void accept_connections(int epfd, int listen_fd) {
    struct sockaddr_storage cli_addr;
    socklen_t cli_len = sizeof(cli_addr);

    while (1) {
        int client_fd = accept(listen_fd, (struct sockaddr *)&cli_addr, &cli_len);
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            break;
        }

        make_socket_nonblocking(client_fd);
        add_fd_to_epoll(epfd, client_fd, EPOLLIN);

        client_t *client = client_create(client_fd);
        dictionary_set(fd_to_client, &(client->fd), client);
    }
}


int main(int argc, char **argv) {
    if (argc != 2) {
        print_server_usage();
        return 1;
    }
    const char *port = argv[1];

    install_sigint_handler();
    signal(SIGPIPE, SIG_IGN);

    server_files = vector_create(NULL, NULL, NULL);
    if (!server_files) {
        perror("vector_create");
        return 1;
    }

    fd_to_client = int_to_shallow_dictionary_create();
    if (!fd_to_client) {
        perror("dictionary_create");
        return 1;
    }

    build_temp_directory();
    if (!temp_dir) return 1;

    listen_fd = create_and_bind_socket(port);
    if (listen_fd < 0) {
        cleanup();
        return 1;
    }
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1");
        cleanup();
        return 1;
    }
    add_fd_to_epoll(epoll_fd, listen_fd, EPOLLIN);

    struct epoll_event events[MAX_EVENTS];
    while (running) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; i++) {
            int current_fd = events[i].data.fd;

            if (current_fd == listen_fd) {
                accept_connections(epoll_fd, listen_fd);
            } else {
                client_t *client = dictionary_get(fd_to_client, &current_fd);
                if (!client) continue;

                if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                    close_and_cleanup(epoll_fd, client);
                    continue;
                }

                if (events[i].events & EPOLLIN) {
                    handle_read_event(epoll_fd, client);
                }
                if ((events[i].events & EPOLLOUT) && client->state != STATE_RECV_HEADER) {
                    handle_write_event(epoll_fd, client);
                }
            }
        }
    }
    cleanup();
    return 0;
}
