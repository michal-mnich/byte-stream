#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "err.h"
#include "protconst.h"
#include "protocol.h"

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fatal("usage: %s <protocol> <port>", argv[0]);
    }

    uint64_t current_total_count;

    struct sockaddr_in server_address, client_address, old_client_address;
    int socket_fd, client_fd;

    bool stop;
    uint64_t left;
    uint64_t expected_packet_no;
    uint32_t recv_packet_count;
    char* packet;

    time_t start;

    // Ignore the SIGPIPE signal (handled in write).
    signal(SIGPIPE, SIG_IGN);

    // Initialize the random number generator.
    srand_init();

    // Parse the arguments.
    uint8_t protocol_id = parse_protocol(argv[1]);
    uint16_t port       = read_port(argv[2]);

    // Prepare the server address structure.
    server_address.sin_family      = AF_INET;           // IPv4
    server_address.sin_addr.s_addr = htonl(INADDR_ANY); // all interfaces
    server_address.sin_port        = htons(port);       // port provided

    if (protocol_id == TCP_ID) {
        socket_fd = tcp_listen(&server_address);
        while (1) {
            client_fd = tcp_accept(socket_fd, &client_address);

            // Dummy loop, "break" will prematurely close the connection.
            do {
                if (!recv_CONN(client_fd, &current_total_count, NULL)) break;
                if (!send_CONACC(client_fd, NULL)) break;

                left               = current_total_count;
                expected_packet_no = START_NO;
                while (left > 0) {
                    if (!recv_DATA(client_fd,
                                   expected_packet_no,
                                   &recv_packet_count,
                                   &packet,
                                   NULL))
                    {
                        if (current_error != ERRTIMEOUT &&
                            current_error != ERRIO)
                            send_RJT(client_fd, expected_packet_no, NULL);
                        break;
                    }
                    print_packet(packet, recv_packet_count);
                    left -= recv_packet_count;
                    expected_packet_no++;
                }
                if (left > 0) break; // receiving loop failed
                if (!send_RCVD(client_fd, NULL)) break;
                debug("received %" PRIu64 " bytes", current_total_count);
            } while (0);
            tcp_disconnect(client_fd, &client_address);
        }
        ASSERT_SYS_OK(close(socket_fd));
        debug("stopped listening on port %" PRIu16,
              ntohs(server_address.sin_port));
    }
    else if (protocol_id == UDP_ID) {
        socket_fd = udp_listen(&server_address);
        while (1) {
            // Dummy loop, "break" will prematurely stop serving the client.
            do {
                if (!recv_CONN(socket_fd,
                               &current_total_count,
                               &client_address))
                    break;
                socket_set_timeout(socket_fd);
                if (!send_CONACC(socket_fd, &client_address)) break;

                old_client_address = client_address;
                stop               = false;
                left               = current_total_count;
                expected_packet_no = START_NO;

                while (left > 0) {
                    start = time(NULL);
                    while (!stop && !recv_DATA(socket_fd,
                                               expected_packet_no,
                                               &recv_packet_count,
                                               &packet,
                                               &client_address))
                    {
                        if (time(NULL) - start >= MAX_WAIT)
                            current_error = ERRTIMEOUT;
                        if (current_error == ERRTIMEOUT) {
                            if (expected_packet_no == START_NO) {
                                if (udpr &&
                                    retransmit_CONACC(socket_fd,
                                                      &client_address,
                                                      expected_packet_no,
                                                      &recv_packet_count,
                                                      &packet))
                                    break;
                                else stop = true;
                            }
                            else {
                                if (udpr &&
                                    retransmit_ACC(socket_fd,
                                                   expected_packet_no - 1,
                                                   &client_address,
                                                   expected_packet_no,
                                                   &recv_packet_count,
                                                   &packet))
                                    break;
                                else stop = true;
                            }
                        }
                        else if (current_error == ERRCONN) {
                            // foreign client sent CONN
                            send_CONRJT(socket_fd, &client_address);
                        }
                        else if (current_error == ERRIO) {
                            // syscall error
                            stop = true;
                        }
                        else if (current_error != ERROLD) {
                            // stop serving the current client, if it came from him
                            if (current_error != ERRSESSION) stop = true;
                            send_RJT(socket_fd,
                                     expected_packet_no,
                                     &client_address);
                        }
                        client_address = old_client_address;
                    }
                    if (stop) break;
                    if (recv_packet_count > left) {
                        error("received too many bytes");
                        send_RJT(socket_fd,
                                 expected_packet_no,
                                 &client_address);
                        stop = true;
                        break;
                    }
                    if (udpr && !send_ACC(socket_fd,
                                          expected_packet_no,
                                          &client_address))
                    {
                        stop = true;
                        break;
                    }
                    print_packet(packet, recv_packet_count);
                    left -= recv_packet_count;
                    expected_packet_no++;
                }
                if (stop) break; // receiving loop failed
                if (!send_RCVD(socket_fd, &client_address)) break;
                debug("received %" PRIu64 " bytes", current_total_count);
            } while (0);
            debug("stopped serving %s:%" PRIu16,
                  inet_ntoa(client_address.sin_addr),
                  ntohs(client_address.sin_port));
            socket_clear_timeout(socket_fd);
        }
        ASSERT_SYS_OK(close(socket_fd));
        debug("stopped listening on port %" PRIu16,
              ntohs(server_address.sin_port));
    }
    else {
        fatal("invalid server protocol: %s", argv[1]);
    }

    return 0;
}
