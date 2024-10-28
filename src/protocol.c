#include <arpa/inet.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "err.h"
#include "protconst.h"
#include "protocol.h"

// 1451 (data payload) + 21 (data header) + 8 (UDP header) + 20 (IP header) = 1500 (MTU)
#define MAX_NO_FRAGMENTS 1451

static char buffer[BUFFER_SIZE];

bool udpr = false;

static bool handle_foreign = false;
static uint64_t foreign_session_id;

static uint64_t current_session_id;
static uint8_t current_protocol_id;

// Generate a random 64-bit unsigned integer.
uint64_t generate_random_uint64(void) {
    uint64_t num = 0;
    for (int i = 0; i < 64; i += (sizeof(int) * 8)) {
        num = (num << (sizeof(int) * 8)) | (uint32_t)rand();
    }
    return num;
}

// Generate a random valid packet count between 1 and 'left'.
uint16_t generate_packet_count(uint64_t left) {
    uint64_t len = generate_random_uint64() % left;
    return len % MAX_PACKET_COUNT + 1;
}

// Match client/server protocols and set UDPR flag.
static bool match_protocols(uint8_t client, uint8_t server) {
    bool cond1 = (client == TCP_ID) && (server == TCP_ID);
    bool cond2 = (client == UDP_ID) && (server == UDP_ID);
    bool cond3 = (client == UDPR_ID) && (server == UDP_ID);

    udpr = cond3;
    if (cond1) debug("operating in tcp mode");
    if (cond2) debug("operating in udp mode");
    if (cond3) debug("operating in udpr mode");

    return (cond1 || cond2 || cond3);
}

// Parse the protocol string, set the current protocol ID and set UDPR flag.
uint8_t parse_protocol(const char* protocol) {
    if (strcmp(protocol, "tcp") == 0) {
        current_protocol_id = TCP_ID;
    }
    else if (strcmp(protocol, "udp") == 0) {
        current_protocol_id = UDP_ID;
    }
    else if (strcmp(protocol, "udpr") == 0) {
        current_protocol_id = UDPR_ID;
        udpr                = true;
    }
    else {
        current_protocol_id = INVAL_ID;
    }

    debug("set current_protocol_id to %u", current_protocol_id);

    return current_protocol_id;
}

// Send CONN packet and set random current session ID.
bool send_CONN(int socket_fd,
               uint64_t total_count,
               struct sockaddr_in* client_address) {
    current_error      = NOERR;
    current_session_id = generate_random_uint64();
    debug("set current_session_id to %" PRIu64, current_session_id);

    static conn_t conn;
    conn.type_id     = CONN_ID;
    conn.session_id  = htobe64(current_session_id);
    conn.protocol_id = current_protocol_id;
    conn.total_count = htobe64(total_count);

    bool tcp_success = current_protocol_id == TCP_ID &&
                       tcp_writen(socket_fd, &conn, sizeof(conn));
    bool udp_success =
        (current_protocol_id == UDP_ID || current_protocol_id == UDPR_ID) &&
        udp_sendto(socket_fd, &conn, sizeof(conn), client_address);
    if (!tcp_success && !udp_success) {
        error("failed to send CONN");
        return false;
    }
    debug("sent CONN");
    return true;
}

// Send CONACC packet.
bool send_CONACC(int socket_fd, struct sockaddr_in* client_address) {
    current_error = NOERR;
    static conacc_t conacc;
    conacc.type_id    = CONACC_ID;
    conacc.session_id = htobe64(current_session_id);

    bool tcp_success = current_protocol_id == TCP_ID &&
                       tcp_writen(socket_fd, &conacc, sizeof(conacc));
    bool udp_success =
        current_protocol_id == UDP_ID &&
        udp_sendto(socket_fd, &conacc, sizeof(conacc), client_address);
    if (!tcp_success && !udp_success) {
        error("failed to send CONACC");
        return false;
    }
    debug("sent CONACC");
    return true;
}

// Send CONRJT packet.
bool send_CONRJT(int socket_fd, struct sockaddr_in* client_address) {
    current_error = NOERR;
    static conrjt_t conrjt;
    conrjt.type_id = CONRJT_ID;
    if (handle_foreign) {
        conrjt.session_id = htobe64(foreign_session_id);
        handle_foreign    = false;
    }
    else {
        error("unexpected CONRJT");
        return false;
    }

    bool tcp_success = current_protocol_id == TCP_ID &&
                       tcp_writen(socket_fd, &conrjt, sizeof(conrjt));
    bool udp_success =
        current_protocol_id == UDP_ID &&
        udp_sendto(socket_fd, &conrjt, sizeof(conrjt), client_address);
    if (!tcp_success && !udp_success) {
        error("failed to send CONRJT");
        return false;
    }

    debug("sent CONRJT");
    return true;
}

// Send DATA packet and actual data from buffer.
bool send_DATA(int socket_fd,
               uint64_t packet_no,
               uint32_t packet_count,
               const char* packet,
               struct sockaddr_in* client_address) {
    current_error = NOERR;
    static data_t data;
    data.type_id      = DATA_ID;
    data.session_id   = htobe64(current_session_id);
    data.packet_no    = htobe64(packet_no);
    data.packet_count = htobe32(packet_count);

    memcpy(buffer, &data, sizeof(data));
    memcpy(buffer + sizeof(data), packet, packet_count);

    bool tcp_success = current_protocol_id == TCP_ID &&
                       tcp_writen(socket_fd, &data, sizeof(data));
    bool udp_success =
        (current_protocol_id == UDP_ID || current_protocol_id == UDPR_ID) &&
        udp_sendto(socket_fd,
                   buffer,
                   sizeof(data) + packet_count,
                   client_address);

    if (tcp_success) {
        if (!tcp_writen(socket_fd, packet, packet_count)) {
            error("failed to send DATA payload");
            return false;
        }
    }

    if (!tcp_success && !udp_success) {
        error("failed to send DATA");
        return false;
    }

    debug("sent DATA (packet_no=%" PRIu64 ", packet_size=%u)",
          packet_no,
          packet_count);
    return true;
}

// Send ACC packet.
bool send_ACC(int socket_fd,
              uint64_t packet_no,
              struct sockaddr_in* client_address) {
    current_error = NOERR;
    static acc_t acc;
    acc.type_id    = ACC_ID;
    acc.session_id = htobe64(current_session_id);
    acc.packet_no  = htobe64(packet_no);

    bool tcp_success = current_protocol_id == TCP_ID &&
                       tcp_writen(socket_fd, &acc, sizeof(acc));
    bool udp_success = current_protocol_id == UDP_ID &&
                       udp_sendto(socket_fd, &acc, sizeof(acc), client_address);
    if (!tcp_success && !udp_success) {
        error("failed to send ACC (packet_no=%" PRIu64 ")", packet_no);
        return false;
    }

    debug("sent ACC (packet_no=%" PRIu64 ")", packet_no);
    return true;
}

// Send RJT packet.
bool send_RJT(int socket_fd,
              uint64_t packet_no,
              struct sockaddr_in* client_address) {
    current_error = NOERR;
    static rjt_t rjt;
    rjt.type_id = RJT_ID;
    if (handle_foreign) {
        rjt.session_id = htobe64(foreign_session_id);
        handle_foreign = false;
    }
    else {
        rjt.session_id = htobe64(current_session_id);
    }
    rjt.packet_no = htobe64(packet_no);

    bool tcp_success = current_protocol_id == TCP_ID &&
                       tcp_writen(socket_fd, &rjt, sizeof(rjt));
    bool udp_success = current_protocol_id == UDP_ID &&
                       udp_sendto(socket_fd, &rjt, sizeof(rjt), client_address);
    if (!tcp_success && !udp_success) {
        error("failed to send RJT");
        return false;
    }

    debug("sent RJT");
    return true;
}

// Send RCVD packet.
bool send_RCVD(int socket_fd, struct sockaddr_in* client_address) {
    current_error = NOERR;
    static rcvd_t rcvd;
    rcvd.type_id    = RCVD_ID;
    rcvd.session_id = htobe64(current_session_id);

    bool tcp_success = current_protocol_id == TCP_ID &&
                       tcp_writen(socket_fd, &rcvd, sizeof(rcvd));
    bool udp_success =
        current_protocol_id == UDP_ID &&
        udp_sendto(socket_fd, &rcvd, sizeof(rcvd), client_address);
    if (!tcp_success && !udp_success) {
        error("failed to send RCVD");
        return false;
    }

    debug("sent RCVD");
    return true;
}

static bool check_type_quiet(char* buf, size_t nrecv, uint8_t expected) {
    if (nrecv >= sizeof(uint8_t)) {
        uint8_t tmp;
        memcpy(&tmp, buf, sizeof(uint8_t));
        if (tmp != expected) {
            return false;
        }
    }
    return true;
}

static bool check_type(char* buf, size_t nrecv, uint8_t expected) {
    if (nrecv >= sizeof(uint8_t)) {
        uint8_t tmp;
        memcpy(&tmp, buf, sizeof(uint8_t));
        if (tmp != expected) {
            error("unexpected type ID: %u", tmp);
            error("expected: %u", expected);
            current_error = ERRTYPE;
            return false;
        }
    }
    return true;
}

static bool check_session_quiet(char* buf, size_t nrecv) {
    if (nrecv >= sizeof(uint64_t) + sizeof(uint8_t)) {
        uint64_t session_id, tmp;
        memcpy(&session_id, buf + sizeof(uint8_t), sizeof(uint64_t));
        tmp = be64toh(session_id);
        if (tmp != current_session_id) {
            foreign_session_id = tmp;
            handle_foreign     = true;
            return false;
        }
    }
    return true;
}

static bool check_session(char* buf, size_t nrecv) {
    if (nrecv >= sizeof(uint64_t) + sizeof(uint8_t)) {
        uint64_t session_id, tmp;
        memcpy(&session_id, buf + sizeof(uint8_t), sizeof(uint64_t));
        tmp = be64toh(session_id);
        if (tmp != current_session_id) {
            error("unexpected session ID: %" PRIu64, tmp);
            error("expected: %" PRIu64, current_session_id);
            current_error      = ERRSESSION;
            foreign_session_id = tmp;
            handle_foreign     = true;
            return false;
        }
    }
    return true;
}

static bool check_size(size_t nrecv, size_t expected) {
    if (nrecv != expected) {
        error("unexpected size: %zu", nrecv);
        error("expected: %zu", expected);
        current_error = ERRSIZE;
        return false;
    }
    return true;
}

static bool check_protocols(uint8_t client, uint8_t server) {
    if (!match_protocols(client, server)) {
        error("client protocol ID %u and server protocol ID %u cannot be "
              "correctly matched",
              client,
              server);
        current_error = ERRPROTOCOL;
        return false;
    }
    return true;
}

static bool check_packet_no(uint64_t packet_no, uint64_t expected) {
    uint64_t tmp = be64toh(packet_no);
    if (tmp != expected) {
        error("unexpected packet number: %" PRIu64, tmp);
        error("expected: %" PRIu64, expected);
        current_error = ERRPACKETNO;
        return false;
    }
    return true;
}

static bool check_packet_count(uint32_t packet_count) {
    uint32_t tmp = be32toh(packet_count);
    if (tmp < 1 || MAX_PACKET_COUNT < tmp) {
        error("invalid packet count: %u", tmp);
        current_error = ERRPACKETCOUNT;
        return false;
    }
    return true;
}

static bool check_foreign_conn(char* buf, size_t nrecv) {
    if (!check_session_quiet(buf, nrecv)) {
        if (check_type_quiet(buf, nrecv, CONN_ID)) {
            error("received CONN from foreign session");
            current_error = ERRCONN;
            return false;
        }
    }
    return true;
}

// Receive CONN packet, match client/server protocols, set current session ID and total count.
bool recv_CONN(int socket_fd,
               uint64_t* current_total_count,
               struct sockaddr_in* client_address) {
    current_error = NOERR;
    bool err;
    static conn_t conn;
    size_t nrecv = BUFFER_SIZE;

    if (current_protocol_id == TCP_ID &&
        tcp_readn(socket_fd, &conn, sizeof(conn)))
    {
        err = !check_type((char*)&conn, sizeof(conn), CONN_ID) ||
              !check_protocols(conn.protocol_id, current_protocol_id);
    }
    else if (current_protocol_id == UDP_ID &&
             udp_recvfrom(socket_fd, buffer, &nrecv, client_address))
    {
        err = !check_type(buffer, nrecv, CONN_ID) ||
              !check_size(nrecv, sizeof(conn));
        if (!err) {
            memcpy(&conn, buffer, sizeof(conn));
            err = !check_protocols(conn.protocol_id, current_protocol_id);
        }
    }
    else {
        err = true;
    }

    if (err) {
        error("failed to receive CONN");
        return false;
    }
    else {
        debug("received CONN");
        current_session_id = be64toh(conn.session_id);
        debug("set current_session_id to %" PRIu64, current_session_id);
        *current_total_count = be64toh(conn.total_count);
        debug("set current_total_count to %" PRIu64, *current_total_count);
        return true;
    }
}

// Receive CONACC packet.
bool recv_CONACC(int socket_fd, struct sockaddr_in* client_address) {
    current_error = NOERR;
    bool err;
    static conacc_t conacc;
    size_t nrecv = BUFFER_SIZE;

    if (current_protocol_id == TCP_ID &&
        tcp_readn(socket_fd, &conacc, sizeof(conacc)))
    {
        err = !check_session((char*)&conacc, sizeof(conacc)) ||
              !check_type((char*)&conacc, sizeof(conacc), CONACC_ID);
    }
    else if ((current_protocol_id == UDP_ID ||
              current_protocol_id == UDPR_ID) &&
             udp_recvfrom(socket_fd, buffer, &nrecv, client_address))
    {
        err = !check_session(buffer, nrecv) ||
              !check_type(buffer, nrecv, CONACC_ID) ||
              !check_size(nrecv, sizeof(conacc_t));
    }
    else {
        err = true;
    }

    if (err) {
        error("failed to receive CONACC");
        return false;
    }
    else {
        debug("received CONACC");
        return true;
    }
}

// Receive DATA packet and actual data to buffer. Set received packet count.
bool recv_DATA(int socket_fd,
               uint64_t expected_packet_no,
               uint32_t* recv_packet_count,
               char** packet,
               struct sockaddr_in* client_address) {
    current_error = NOERR;
    bool err;
    static data_t data;
    size_t nrecv = BUFFER_SIZE;

    if (current_protocol_id == TCP_ID &&
        tcp_readn(socket_fd, &data, sizeof(data)))
    {
        err = !check_session((char*)&data, sizeof(data)) ||
              !check_type((char*)&data, sizeof(data), DATA_ID) ||
              !check_packet_no(data.packet_no, expected_packet_no) ||
              !check_packet_count(data.packet_count);

        if (!err && !tcp_readn(socket_fd,
                               buffer + sizeof(data),
                               be32toh(data.packet_count)))
        {
            err = true;
        }
    }
    else if (current_protocol_id == UDP_ID && !udpr &&
             udp_recvfrom(socket_fd, buffer, &nrecv, client_address))
    {
        err = !check_foreign_conn(buffer, nrecv) ||
              !check_session(buffer, nrecv) ||
              !check_type(buffer, nrecv, DATA_ID) ||
              !check_size((nrecv >= sizeof(data_t)) * sizeof(data_t),
                          sizeof(data_t));

        if (!err) {
            memcpy(&data, buffer, sizeof(data));
            err = !check_packet_no(data.packet_no, expected_packet_no) ||
                  !check_packet_count(data.packet_count) ||
                  !check_size(nrecv, sizeof(data) + be32toh(data.packet_count));
        }
    }
    else if (current_protocol_id == UDP_ID && udpr &&
             udp_recvfrom(socket_fd, buffer, &nrecv, client_address))
    {
        // Check priority error conditions (foreign CONN packet, foreign session ID)
        err =
            !check_foreign_conn(buffer, nrecv) || !check_session(buffer, nrecv);

        // Check for old CONN packet from current session
        if (!err && check_type_quiet(buffer, nrecv, CONN_ID)) {
            current_error = ERROLD;
            err           = true;
            error("received old CONN packet");
        }

        // Check other error conditions (invalid packet type, invalid packet size)
        err = err || !check_type(buffer, nrecv, DATA_ID) ||
              !check_size((nrecv >= sizeof(data_t)) * sizeof(data_t),
                          sizeof(data_t));

        if (!err) {
            // Header is correct
            memcpy(&data, buffer, sizeof(data));

            // Check for old DATA packet from current session
            if (be64toh(data.packet_no) < expected_packet_no) {
                current_error = ERROLD;
                err           = true;
                error("received old DATA packet (packet_no=%" PRIu64 ")",
                      be64toh(data.packet_no));
            }

            // Check for invalid packet number, packet count, and packet size
            err = err || !check_packet_no(data.packet_no, expected_packet_no) ||
                  !check_packet_count(data.packet_count) ||
                  !check_size(nrecv, sizeof(data) + be32toh(data.packet_count));
        }
    }
    else {
        err = true;
    }

    if (err) {
        error("failed to receive DATA");
        return false;
    }
    else {
        *packet            = buffer + sizeof(data);
        *recv_packet_count = be32toh(data.packet_count);
        debug("received DATA (packet_no=%" PRIu64 ", packet_count=%u)",
              be64toh(data.packet_no),
              *recv_packet_count);
        return true;
    }
}

// Receive ACC packet.
bool recv_ACC(int socket_fd,
              uint64_t expected_packet_no,
              struct sockaddr_in* client_address) {
    current_error = NOERR;
    bool err;
    static acc_t acc;
    size_t nrecv = BUFFER_SIZE;

    if (current_protocol_id == TCP_ID &&
        tcp_readn(socket_fd, &acc, sizeof(acc)))
    {
        err = !check_session((char*)&acc, sizeof(acc)) ||
              !check_type((char*)&acc, sizeof(acc), ACC_ID) ||
              !check_packet_no(acc.packet_no, expected_packet_no);
    }
    else if (current_protocol_id == UDP_ID &&
             udp_recvfrom(socket_fd, buffer, &nrecv, client_address))
    {
        err = !check_session(buffer, nrecv) ||
              !check_type(buffer, nrecv, ACC_ID) ||
              !check_size(nrecv, sizeof(acc_t));
        if (!err) {
            memcpy(&acc, buffer, sizeof(acc));
            err = !check_packet_no(acc.packet_no, expected_packet_no);
        }
    }
    else if (current_protocol_id == UDPR_ID &&
             udp_recvfrom(socket_fd, buffer, &nrecv, client_address))
    {
        err = !check_session(buffer, nrecv);

        if (!err && check_type_quiet(buffer, nrecv, CONACC_ID)) {
            current_error = ERROLD;
            err           = true;
            error("received old CONACC packet");
        }

        err = err || !check_type(buffer, nrecv, ACC_ID) ||
              !check_size(nrecv, sizeof(acc_t));

        if (!err) {
            memcpy(&acc, buffer, sizeof(acc));
            if (be64toh(acc.packet_no) < expected_packet_no) {
                current_error = ERROLD;
                err           = true;
                error("received old ACC packet (packet_no=%" PRIu64 ")",
                      be64toh(acc.packet_no));
            }
            err = err || !check_packet_no(acc.packet_no, expected_packet_no);
        }
    }
    else {
        err = true;
    }

    if (err) {
        error("failed to receive ACC (packet_no=%" PRIu64 ")",
              expected_packet_no);
        return false;
    }
    else {
        debug("received ACC (packet_no=%" PRIu64 ")", expected_packet_no);
        return true;
    }
}

// Receive RCVD packet.
bool recv_RCVD(int socket_fd, struct sockaddr_in* client_address) {
    current_error = NOERR;
    bool err;
    static rcvd_t rcvd;
    size_t nrecv = BUFFER_SIZE;

    if (current_protocol_id == TCP_ID &&
        tcp_readn(socket_fd, &rcvd, sizeof(rcvd)))
    {
        err = !check_session((char*)&rcvd, sizeof(rcvd)) ||
              !check_type((char*)&rcvd, sizeof(rcvd), RCVD_ID);
    }
    else if (current_protocol_id == UDP_ID &&
             udp_recvfrom(socket_fd, buffer, &nrecv, client_address))
    {
        err = !check_session(buffer, nrecv) ||
              !check_type(buffer, nrecv, RCVD_ID) ||
              !check_size(nrecv, sizeof(rcvd_t));
    }
    else if (current_protocol_id == UDPR_ID &&
             udp_recvfrom(socket_fd, buffer, &nrecv, client_address))
    {
        err = !check_session(buffer, nrecv);

        if (!err && check_type_quiet(buffer, nrecv, CONACC_ID)) {
            current_error = ERROLD;
            err           = true;
            error("received old CONACC packet");
        }

        if (!err && check_type_quiet(buffer, nrecv, ACC_ID)) {
            current_error = ERROLD;
            err           = true;
            error("received old ACC packet (packet_no=%" PRIu64 ")",
                  be64toh(((acc_t*)buffer)->packet_no));
        }

        err = err || !check_type(buffer, nrecv, RCVD_ID) ||
              !check_size(nrecv, sizeof(rcvd_t));
    }
    else {
        err = true;
    }

    if (err) {
        error("failed to receive RCVD");
        return false;
    }
    else {
        debug("received RCVD");
        return true;
    }
}

bool retransmit_CONN(int socket_fd,
                     uint64_t total_count,
                     struct sockaddr_in* client_address) {
    struct sockaddr_in old_client_address = *client_address;
    for (int i = 0; i < MAX_RETRANSMITS; i++) {
        debug("attempt %d to retransmit CONN", i + 1);
        if (!send_CONN(socket_fd, total_count, client_address)) break;
        if (recv_CONACC(socket_fd, client_address)) {
            debug("retransmitted CONN");
            return true;
        }
        else if (current_error == ERRSESSION) i--;
        else if (current_error != ERRTIMEOUT) break;

        *client_address = old_client_address;
    }
    error("failed to retransmit CONN");
    return false;
}

bool retransmit_DATA(int socket_fd,
                     uint64_t packet_no,
                     uint32_t packet_count,
                     const char* packet,
                     struct sockaddr_in* client_address) {
    struct sockaddr_in old_client_address = *client_address;
    for (int i = 0; i < MAX_RETRANSMITS; i++) {
        debug("attempt %d to retransmit DATA (packet_no=%" PRIu64
              ", packet_count=%u)",
              i + 1,
              packet_no,
              packet_count);
        if (!send_DATA(socket_fd,
                       packet_no,
                       packet_count,
                       packet,
                       client_address))
            break;
        if (recv_ACC(socket_fd, packet_no, client_address)) {
            debug("retransmitted DATA (packet_no=%" PRIu64 ", packet_count=%u)",
                  packet_no,
                  packet_count);
            return true;
        }
        else if (current_error == ERRSESSION || current_error == ERROLD) i--;
        else if (current_error != ERRTIMEOUT) break;

        *client_address = old_client_address;
    }
    error("failed to retransmit DATA (packet_no=%" PRIu64 ", packet_count=%u)",
          packet_no,
          packet_count);
    return false;
}

bool retransmit_ACC(int socket_fd,
                    uint64_t packet_no,
                    struct sockaddr_in* client_address,
                    uint64_t expected_packet_no,
                    uint32_t* recv_packet_count,
                    char** packet) {
    struct sockaddr_in old_client_address = *client_address;
    bool stop                             = false;
    for (int i = 0; !stop && i < MAX_RETRANSMITS; i++) {
        debug("attempt %d to retransmit ACC (packet_no=%" PRIu64 ")",
              i + 1,
              packet_no);
        if (!send_ACC(socket_fd, packet_no, client_address)) break;
        if (recv_DATA(socket_fd,
                      expected_packet_no,
                      recv_packet_count,
                      packet,
                      client_address))
        {
            debug("retransmitted ACC (packet_no=%" PRIu64 ")", packet_no);
            return true;
        }
        else if (current_error == ERRCONN) {
            // foreign client sent CONN
            send_CONRJT(socket_fd, client_address);
            i--;
        }
        else if (current_error == ERRIO) {
            // syscall error
            break;
        }
        else if (current_error == ERROLD) {
            i--;
        }
        else if (current_error != ERRTIMEOUT) {
            // stop serving the current client, if it came from him
            if (current_error != ERRSESSION) stop = true;
            else i--;
            send_RJT(socket_fd, expected_packet_no, client_address);
        }

        *client_address = old_client_address;
    }
    error("failed to retransmit ACC (packet_no=%" PRIu64 ")", packet_no);
    return false;
}

bool retransmit_CONACC(int socket_fd,
                       struct sockaddr_in* client_address,
                       uint64_t expected_packet_no,
                       uint32_t* recv_packet_count,
                       char** packet) {
    struct sockaddr_in old_client_address = *client_address;
    bool stop                             = false;
    for (int i = 0; !stop && i < MAX_RETRANSMITS; i++) {
        debug("attempt %d to retransmit CONACC", i + 1);
        if (!send_CONACC(socket_fd, client_address)) break;
        if (recv_DATA(socket_fd,
                      expected_packet_no,
                      recv_packet_count,
                      packet,
                      client_address))
        {
            debug("retransmitted CONACC");
            return true;
        }
        else if (current_error == ERRCONN) {
            // foreign client sent CONN
            send_CONRJT(socket_fd, client_address);
            i--;
        }
        else if (current_error == ERRIO) {
            // syscall error
            break;
        }
        else if (current_error == ERROLD) {
            i--;
        }
        else if (current_error != ERRTIMEOUT) {
            // stop serving the current client, if it came from him
            if (current_error != ERRSESSION) stop = true;
            else i--;
            send_RJT(socket_fd, expected_packet_no, client_address);
        }

        *client_address = old_client_address;
    }
    error("failed to retransmit ACC");
    return false;
}
