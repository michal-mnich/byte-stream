#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <inttypes.h>
#include <stdbool.h>

#define CONN_ID   1
#define CONACC_ID 2
#define CONRJT_ID 3
#define DATA_ID   4
#define ACC_ID    5
#define RJT_ID    6
#define RCVD_ID   7

#define TCP_ID  1
#define UDP_ID  2
#define UDPR_ID 3

#define INVAL_ID 0

#define START_NO         0
#define MAX_PACKET_COUNT 64000

typedef struct __attribute__((__packed__)) {
    uint8_t type_id;
    uint64_t session_id;
    uint8_t protocol_id;
    uint64_t total_count;
} conn_t;

typedef struct __attribute__((__packed__)) {
    uint8_t type_id;
    uint64_t session_id;
} conacc_t;

typedef struct __attribute__((__packed__)) {
    uint8_t type_id;
    uint64_t session_id;
} conrjt_t;

typedef struct __attribute__((__packed__)) {
    uint8_t type_id;
    uint64_t session_id;
    uint64_t packet_no;
    uint32_t packet_count;
} data_t;

typedef struct __attribute__((__packed__)) {
    uint8_t type_id;
    uint64_t session_id;
    uint64_t packet_no;
} acc_t;

typedef struct __attribute__((__packed__)) {
    uint8_t type_id;
    uint64_t session_id;
    uint64_t packet_no;
} rjt_t;

typedef struct __attribute__((__packed__)) {
    uint8_t type_id;
    uint64_t session_id;
} rcvd_t;

extern bool udpr;

uint64_t generate_random_uint64(void);
uint16_t generate_packet_count(uint64_t left);
uint8_t parse_protocol(const char* protocol);

bool send_CONN(int socket_fd,
               uint64_t total_count,
               struct sockaddr_in* client_address);
bool send_CONACC(int socket_fd, struct sockaddr_in* client_address);
bool send_CONRJT(int socket_fd, struct sockaddr_in* client_address);
bool send_DATA(int socket_fd,
               uint64_t packet_no,
               uint32_t packet_count,
               const char* buffer,
               struct sockaddr_in* client_address);
bool send_ACC(int socket_fd,
              uint64_t packet_no,
              struct sockaddr_in* client_address);
bool send_RJT(int socket_fd,
              uint64_t packet_no,
              struct sockaddr_in* client_address);
bool send_RCVD(int socket_fd, struct sockaddr_in* client_address);

bool recv_CONN(int socket_fd,
               uint64_t* current_total_count,
               struct sockaddr_in* client_address);
bool recv_CONACC(int socket_fd, struct sockaddr_in* client_address);
bool recv_DATA(int socket_fd,
               uint64_t expected_packet_no,
               uint32_t* recv_packet_count,
               char** packet,
               struct sockaddr_in* client_address);
bool recv_ACC(int socket_fd,
              uint64_t expected_packet_no,
              struct sockaddr_in* client_address);
bool recv_RCVD(int socket_fd, struct sockaddr_in* client_address);

bool retransmit_CONN(int socket_fd,
                     uint64_t total_count,
                     struct sockaddr_in* client_address);
bool retransmit_DATA(int socket_fd,
                     uint64_t packet_no,
                     uint32_t packet_count,
                     const char* packet,
                     struct sockaddr_in* client_address);
bool retransmit_ACC(int socket_fd,
                    uint64_t packet_no,
                    struct sockaddr_in* client_address,
                    uint64_t expected_packet_no,
                    uint32_t* recv_packet_count,
                    char** packet);
bool retransmit_CONACC(int socket_fd,
                       struct sockaddr_in* client_address,
                       uint64_t expected_packet_no,
                       uint32_t* recv_packet_count,
                       char** packet);

#endif
