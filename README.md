# Byte Stream Transmission Protocol (PPCB)

## Overview

The Byte Stream Transmission Protocol (PPCB) is designed for transmitting byte streams between a client and a server. It uses TCP or UDP as the underlying transport protocol, with UDP supporting a simple retransmission mechanism. This repository contains the implementation of the PPCB protocol in pure C.

## Protocol Description

PPCB facilitates the transmission of byte streams in packets ranging from 1 to 64,000 bytes. Each packet can have a different length. The communication process is as follows:

### Connection Establishment

- **TCP**: The client establishes a TCP connection to the server. If the server is already handling another connection, it will not accept new connections until the current one is finished. If the client fails to establish a connection, it terminates.
- **UDP**: The client sends a `CONN` packet. The server responds with a `CONACC` packet if it accepts the connection. A UDP server may reject the connection with a `CONRJT` packet if it is busy, causing the client to terminate.

### Data Transmission

- The client sends `DATA` packets of non-zero length.
- The server verifies the `DATA` packet's validity and origin. If invalid, the server sends an `RJT` packet and stops handling the connection.
- A valid `DATA` packet is acknowledged by a UDP server with an `ACC` packet. TCP and non-retransmitting UDP servers do not send acknowledgments.
- The client waits for an `ACC` packet before sending the next `DATA` packet in the case of UDP with retransmission.

### Termination

- After receiving all data, the TCP server sends an `RCVD` packet, closes the connection, and handles new connections. UDP servers send an `RCVD` packet and handle new connections.
- The client terminates after sending all data and receiving an `RCVD` packet.

### Retransmission Mechanism

If an acknowledgment is not received within `MAX_WAIT` seconds, the data is retransmitted up to `MAX_RETRANSMITS` times. If unsuccessful, the connection is terminated.

## Packet Structure

Packets consist of fields of specified lengths in a defined order, without padding between fields:

- **CONN**: Connection initiation (Client -> Server)
  - Packet type ID: 8 bits (value: 1)
  - Session ID: 64 bits
  - Protocol ID: 8 bits (TCP: 1, UDP: 2, UDP with retransmission: 3)
  - Byte stream length: 64 bits

- **CONACC**: Connection acceptance (Server -> Client)
  - Packet type ID: 8 bits (value: 2)
  - Session ID: 64 bits

- **CONRJT**: Connection rejection (Server -> Client)
  - Packet type ID: 8 bits (value: 3)
  - Session ID: 64 bits

- **DATA**: Data packet (Client -> Server)
  - Packet type ID: 8 bits (value: 4)
  - Session ID: 64 bits
  - Packet number: 64 bits
  - Data length: 32 bits
  - Data: Variable length

- **ACC**: Data packet acknowledgment (Server -> Client)
  - Packet type ID: 8 bits (value: 5)
  - Session ID: 64 bits
  - Packet number: 64 bits

- **RJT**: Data packet rejection (Server -> Client)
  - Packet type ID: 8 bits (value: 6)
  - Session ID: 64 bits
  - Packet number: 64 bits

- **RCVD**: Byte stream receipt acknowledgment (Server -> Client)
  - Packet type ID: 8 bits (value: 7)
  - Session ID: 64 bits

## Programs

Two programs are provided: a client and a server.

### Server

The server accepts two parameters:

1. Protocol (`tcp` or `udp`)
2. Port number

The server listens on the specified port and handles connections according to the protocol. Data from `DATA` packets is printed to `stdout` upon receiving a complete packet. The server handles one connection at a time and prints only the received byte stream.

### Client

The client accepts three parameters:

1. Protocol (`tcp`, `udp`, or `udpr`)
2. Server address (numeric or hostname)
3. Port number

The client reads data from `stdin` into a buffer and then transmits it according to the protocol. After sending the data and receiving an `RCVD` acknowledgment, the client terminates.

### Error Handling

Communication errors are printed to `stderr` with the prefix "ERROR:". The client terminates upon encountering communication errors. The server continues to handle new connections if possible. Other errors (e.g., file reading, memory allocation) are handled similarly.

## Compilation and Execution

Programs should be compiled without warnings and run correctly on the target server.

### Building

To build the project, run (in the `src` directory):

```sh
make
```

This will generate the `ppcbs` (server) and `ppcbc` (client) executables.

## Constants

Constants `MAX_WAIT` and `MAX_RETRANSMITS` are declared in `protconst.h`.

## License

This project is licensed under the MIT License. See the LICENSE file for details.
