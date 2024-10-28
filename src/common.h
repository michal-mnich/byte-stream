#ifndef COMMON_H
#define COMMON_H

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

#define BUFFER_SIZE 65536

void srand_init(void);

void read_data_from_stdin(char** buf, uint64_t* length);
uint16_t read_port(char const* string);
struct sockaddr_in get_server_address(char const* host, uint16_t port);
bool tcp_readn(int fd, void* vptr, size_t n);
bool tcp_writen(int fd, const void* vptr, size_t n);

void print_packet(char* packet, uint32_t packet_count);

void socket_set_timeout(int socket_fd);
void socket_clear_timeout(int socket_fd);

int tcp_listen(struct sockaddr_in* server_address);
int tcp_accept(int socket_fd, struct sockaddr_in* client_address);
int tcp_connect_to_server(struct sockaddr_in* server_address);
void tcp_disconnect(int socket_fd, struct sockaddr_in* address);

int udp_connect_to_server(struct sockaddr_in* server_address);
int udp_listen(struct sockaddr_in* server_address);

bool udp_recvfrom(int fd,
                  void* vptr,
                  size_t* n,
                  struct sockaddr_in* client_address);
bool udp_sendto(int fd,
                const void* vptr,
                size_t n,
                struct sockaddr_in* client_address);
#endif
