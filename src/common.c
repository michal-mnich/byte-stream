#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "err.h"
#include "protconst.h"

#define QUEUE_LENGTH 5

// Robert Jenkins' 96 bit Mix Function
static unsigned long mix(unsigned long a, unsigned long b, unsigned long c) {
    a = a - b;
    a = a - c;
    a = a ^ (c >> 13);
    b = b - c;
    b = b - a;
    b = b ^ (a << 8);
    c = c - a;
    c = c - b;
    c = c ^ (b >> 13);
    a = a - b;
    a = a - c;
    a = a ^ (c >> 12);
    b = b - c;
    b = b - a;
    b = b ^ (a << 16);
    c = c - a;
    c = c - b;
    c = c ^ (b >> 5);
    a = a - b;
    a = a - c;
    a = a ^ (c >> 3);
    b = b - c;
    b = b - a;
    b = b ^ (a << 10);
    c = c - a;
    c = c - b;
    c = c ^ (b >> 15);
    return c;
}

// Initializes the random number generator with a seed based on the current time and process ID.
void srand_init(void) {
    unsigned long seed = mix(clock(), time(NULL), getpid());
    srand(seed);
}

/**
 * Reads data of arbitrary length from stdin and saves it to a dynamically allocated buffer.
 * The length of the data in bytes is saved to the provided ength variable.
 *
 * @param[out] buf Pointer to the dynamically allocated buffer containing the read data.
 * @param[out] length Pointer to a variable where the data length in bytes will be stored.
 */
void read_data_from_stdin(char** buf, uint64_t* length) {
    size_t buf_size     = BUFFER_SIZE; // Initial buffer size
    uint64_t bytes_read = 0;

    // Allocate initial buffer
    ASSERT_MALLOC_OK(*buf = malloc(buf_size));

    int ch;
    while ((ch = getchar()) != EOF) {
        // Resize buffer if necessary
        if (bytes_read == buf_size) {
            buf_size *= 2;
            ASSERT_MALLOC_OK(*buf = realloc(*buf, buf_size));
        }
        // Append the character to the buffer
        (*buf)[bytes_read++] = ch;
    }
    *length = bytes_read; // Save the total bytes read
}

uint16_t read_port(char const* string) {
    char* endptr;
    errno              = 0;
    unsigned long port = strtoul(string, &endptr, 10);
    if (errno != 0 || *endptr != 0 || port > UINT16_MAX) {
        fatal("%s is not a valid port number", string);
    }
    return (uint16_t)port;
}

struct sockaddr_in get_server_address(char const* host, uint16_t port) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family   = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* address_result;
    int errcode = getaddrinfo(host, NULL, &hints, &address_result);
    if (errcode != 0) {
        fatal("getaddrinfo: %s", gai_strerror(errcode));
    }

    struct sockaddr_in send_address;
    send_address.sin_family      = AF_INET; // IPv4
    send_address.sin_addr.s_addr =          // IP address
        ((struct sockaddr_in*)(address_result->ai_addr))->sin_addr.s_addr;
    send_address.sin_port = htons(port); // port from the command line

    freeaddrinfo(address_result);

    return send_address;
}

// Read n bytes from a descriptor, return false if failed or timeout.
bool tcp_readn(int fd, void* vptr, size_t n) {
    ssize_t nleft, nread;
    char* ptr;

    ptr   = vptr;
    nleft = n;

    while (nleft > 0) {
        nread = read(fd, ptr, nleft);
        if (nread < 0 && errno == EINTR) {
            // interrupted by signal
            continue;
        }
        if (nread < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // timeout
            error("%s: timeout", __func__);
            current_error = ERRTIMEOUT;
            return false;
        }
        else if (nread < 0) {
            error("%s: failed", __func__);
            current_error = ERRIO;
            return false;
        }
        else if (nread == 0) {
            error("%s: connection closed by peer", __func__);
            current_error = ERRIO;
            return false;
        }
        nleft -= nread;
        ptr += nread;
    }
    return true;
}

// Write n bytes to a descriptor, return false if failed
bool tcp_writen(int fd, const void* vptr, size_t n) {
    ssize_t nleft, nwritten;
    const char* ptr;

    ptr   = vptr;
    nleft = n;
    while (nleft > 0) {
        nwritten = write(fd, ptr, nleft);
        if (nwritten < 0 && errno == EINTR) {
            continue;
        }
        else if (nwritten < 0) {
            error("%s: failed", __func__);
            current_error = ERRIO;
            return false;
        }
        nleft -= nwritten;
        ptr += nwritten;
    }
    return true;
}

void print_packet(char* packet, uint32_t packet_count) {
    if (!tcp_writen(STDOUT_FILENO, packet, packet_count)) fatal("write to stdout failed");
    fflush(stdout);
}

void socket_set_timeout(int socket_fd) {
    struct timeval to = {.tv_sec = MAX_WAIT, .tv_usec = 0};
    ASSERT_SYS_OK(
        setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to));
}

void socket_clear_timeout(int socket_fd) {
    struct timeval to = {.tv_sec = 0, .tv_usec = 0};
    ASSERT_SYS_OK(
        setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to));
}

int tcp_listen(struct sockaddr_in* server_address) {
    int socket_fd;

    // Create a socket for listening.
    ASSERT_SYS_OK(socket_fd = socket(AF_INET, SOCK_STREAM, 0));

    // Set the socket to be bindable to the same address (no TIME_WAIT).
    ASSERT_SYS_OK(setsockopt(socket_fd,
                             SOL_SOCKET,
                             SO_REUSEADDR,
                             &(int){1},
                             sizeof(int)));

    // Bind the socket to the provided address.
    ASSERT_SYS_OK(bind(socket_fd,
                       (struct sockaddr*)server_address,
                       (socklen_t)sizeof(*server_address)));

    // Listen for incoming connections.
    ASSERT_SYS_OK(listen(socket_fd, QUEUE_LENGTH));

    // Get the address that the server is actually listening on.
    ASSERT_SYS_OK(getsockname(socket_fd,
                              (struct sockaddr*)server_address,
                              &((socklen_t){sizeof(*server_address)})));

    debug("listening on port %" PRIu16, ntohs(server_address->sin_port));

    return socket_fd;
}

int tcp_accept(int socket_fd, struct sockaddr_in* client_address) {
    int client_fd;

    // Accept a connection from client.
    ASSERT_SYS_OK(client_fd = accept(socket_fd,
                                     (struct sockaddr*)client_address,
                                     &((socklen_t){sizeof(*client_address)})));

    debug("connected to %s:%" PRIu16,
          inet_ntoa(client_address->sin_addr),
          ntohs(client_address->sin_port));

    socket_set_timeout(client_fd);

    return client_fd;
}

// Connect to a server and return the socket file descriptor.
int tcp_connect_to_server(struct sockaddr_in* server_address) {
    int socket_fd;

    // Create a socket.
    ASSERT_SYS_OK(socket_fd = socket(AF_INET, SOCK_STREAM, 0));

    // Connect to the server.
    ASSERT_SYS_OK(connect(socket_fd,
                          (struct sockaddr*)server_address,
                          (socklen_t)sizeof(*server_address)));

    debug("connected to %s:%" PRIu16,
          inet_ntoa(server_address->sin_addr),
          ntohs(server_address->sin_port));

    socket_set_timeout(socket_fd);

    return socket_fd;
}

void tcp_disconnect(int socket_fd, struct sockaddr_in* address) {
    ASSERT_SYS_OK(close(socket_fd));
    debug("disconnected from %s:%" PRIu16,
          inet_ntoa(address->sin_addr),
          ntohs(address->sin_port));
}

int udp_listen(struct sockaddr_in* server_address) {
    int socket_fd;

    // Create a socket for listening.
    ASSERT_SYS_OK(socket_fd = socket(AF_INET, SOCK_DGRAM, 0));

    // Bind the socket to the provided address.
    ASSERT_SYS_OK(bind(socket_fd,
                       (struct sockaddr*)server_address,
                       (socklen_t)sizeof(*server_address)));

    // Get the address that the server is actually listening on.
    ASSERT_SYS_OK(getsockname(socket_fd,
                              (struct sockaddr*)server_address,
                              &((socklen_t){sizeof(*server_address)})));

    debug("listening on port %" PRIu16, ntohs(server_address->sin_port));

    return socket_fd;
}

int udp_connect_to_server(struct sockaddr_in* server_address) {
    int socket_fd;

    // Create a socket.
    ASSERT_SYS_OK(socket_fd = socket(AF_INET, SOCK_DGRAM, 0));

    // Connect to the server.
    ASSERT_SYS_OK(connect(socket_fd,
                          (struct sockaddr*)server_address,
                          (socklen_t)sizeof(*server_address)));
    debug("connected to %s:%" PRIu16,
          inet_ntoa(server_address->sin_addr),
          ntohs(server_address->sin_port));

    socket_set_timeout(socket_fd);

    return socket_fd;
}

bool udp_recvfrom(int fd,
                  void* buf,
                  size_t* n,
                  struct sockaddr_in* client_address) {
    ssize_t nread;

    nread = recvfrom(fd,
                     buf,
                     *n,
                     0,
                     (struct sockaddr*)client_address,
                     &((socklen_t){sizeof(*client_address)}));
    if (nread < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        error("%s: timeout", __func__);
        current_error = ERRTIMEOUT;
        return false;
    }
    else if (nread < 0) {
        error("%s: failed", __func__);
        current_error = ERRIO;
        return false;
    }
    *n = (size_t)nread;

    return true;
}

bool udp_sendto(int fd,
                const void* buf,
                size_t n,
                struct sockaddr_in* client_address) {
    ssize_t nwritten;

    nwritten = sendto(fd,
                      buf,
                      n,
                      0,
                      (struct sockaddr*)client_address,
                      (socklen_t)sizeof(*client_address));
    if (nwritten < 0) {
        error("%s: failed", __func__);
        current_error = ERRIO;
        return false;
    }
    else if ((size_t)nwritten != n) {
        error("%s: incomplete write", __func__);
        current_error = ERRIO;
        return false;
    }

    return true;
}
