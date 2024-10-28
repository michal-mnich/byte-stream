#include <arpa/inet.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "err.h"
#include "protconst.h"
#include "protocol.h"

int main(int argc, char* argv[]) {
    if (argc != 4) {
        fatal("usage: %s <protocol> <host> <port>", argv[0]);
    }

    int socket_fd;
    struct sockaddr_in server_address, old_server_address;

    char* input;
    uint64_t input_size;
    bool success = false;
    bool stop    = false;

    uint16_t generated_count;
    uint64_t left;
    uint64_t sent;
    uint64_t current_packet_no;

    time_t start;

    // Ignore the SIGPIPE signal (handled in write).
    signal(SIGPIPE, SIG_IGN);

    // Initialize the random number generator.
    srand_init();

    // Read data from standard input (of arbitrary length).
    read_data_from_stdin(&input, &input_size);

    // Parse the arguments
    uint8_t protocol_id = parse_protocol(argv[1]);
    const char* host    = argv[2];
    uint16_t port       = read_port(argv[3]);

    // Prepare the server address structure.
    server_address = get_server_address(host, port);

    if (protocol_id == TCP_ID) {
        socket_fd = tcp_connect_to_server(&server_address);

        // Dummy loop, "break" will prematurely close the connection.
        do {
            if (!send_CONN(socket_fd, input_size, NULL)) break;
            if (!recv_CONACC(socket_fd, NULL)) break;

            left              = input_size;
            sent              = 0;
            current_packet_no = START_NO;
            while (left > 0) {
                generated_count = generate_packet_count(left);
                if (!send_DATA(socket_fd,
                               current_packet_no,
                               generated_count,
                               input + sent,
                               NULL))
                    break;
                left -= generated_count;
                sent += generated_count;
                current_packet_no++;
            }
            if (left > 0) break; // sending loop failed
            debug("sent %" PRIu64 " bytes", input_size);
            if (!recv_RCVD(socket_fd, NULL)) break;
            success = true;
        } while (0);
        tcp_disconnect(socket_fd, &server_address);
    }
    else if (protocol_id == UDP_ID || protocol_id == UDPR_ID) {
        do {
            socket_fd = udp_connect_to_server(&server_address);

            old_server_address = server_address;

            if (!send_CONN(socket_fd, input_size, &server_address)) break;
            start = time(NULL);
            while (!stop && !recv_CONACC(socket_fd, &server_address)) {
                if (time(NULL) - start >= MAX_WAIT) current_error = ERRTIMEOUT;
                // in case of foreign server
                server_address = old_server_address;
                if (current_error == ERRTIMEOUT) {
                    if (udpr &&
                        retransmit_CONN(socket_fd, input_size, &server_address))
                        break;
                    else stop = true;
                }
                else if (current_error != ERRSESSION) {
                    stop = true;
                }
            }
            if (stop) break;

            left              = input_size;
            sent              = 0;
            current_packet_no = START_NO;
            while (left > 0) {
                generated_count = generate_packet_count(left);
                if (!send_DATA(socket_fd,
                               current_packet_no,
                               generated_count,
                               input + sent,
                               &server_address))
                    break;
                start = time(NULL);
                while (udpr && !stop &&
                       !recv_ACC(socket_fd, current_packet_no, &server_address))
                {
                    if (time(NULL) - start >= MAX_WAIT)
                        current_error = ERRTIMEOUT;
                    // in case of foreign server
                    server_address = old_server_address;
                    if (current_error == ERRTIMEOUT) {
                        if (udpr && retransmit_DATA(socket_fd,
                                                    current_packet_no,
                                                    generated_count,
                                                    input + sent,
                                                    &server_address))
                            break;
                        else stop = true;
                    }
                    else if (current_error != ERRSESSION &&
                             current_error != ERROLD)
                    {
                        stop = true;
                    }
                }
                if (stop) break;
                left -= generated_count;
                sent += generated_count;
                current_packet_no++;
            }
            if (stop || left > 0) break; // sending loop failed
            debug("sent %" PRIu64 " bytes", input_size);
            start = time(NULL);
            while (!stop && !recv_RCVD(socket_fd, &server_address)) {
                if (time(NULL) - start >= MAX_WAIT) current_error = ERRTIMEOUT;
                // in case of foreign server
                server_address = old_server_address;
                if (current_error != ERRSESSION && current_error != ERROLD) {
                    stop = true;
                }
            }
            if (stop) break;
            success = true;
        } while (0);
    }
    else {
        error("invalid client protocol: %s", argv[1]);
    }

    free(input);

    return success ? 0 : 1;
}
