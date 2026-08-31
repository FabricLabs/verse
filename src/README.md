# Fabric NOISE Protocol Server and Client

This is an implementation of a server that uses the NOISE protocol for encrypted communication and processes Fabric messages starting with magic bytes `c0d3f33d`.

## About the Noise Protocol

The Noise Protocol Framework is a framework for building crypto protocols. Noise-C is a plain C implementation of this protocol by Rhys Weatherley. It provides a secure way to establish encrypted communication channels between peers.

This implementation uses the Noise_XX pattern, which is a symmetric pattern that provides mutual authentication.

## Prerequisites

To build and run the server and client, you need the following dependencies:

- C compiler (gcc or clang)
- NOISE protocol implementation library (libnoise-protocol)
- libsodium (dependency of libnoise-protocol)

On Debian/Ubuntu, you can install the libsodium dependency with:

```bash
sudo apt-get install gcc make libsodium-dev
```

Then install the Noise-C library from source:

```bash
git clone https://github.com/rweather/noise-c.git
cd noise-c
./autogen.sh
./configure
make
sudo make install
sudo ldconfig
```

On macOS, you may need to specify the include and library paths explicitly, which our Makefile now handles.

## Building

To build both the server and client:

```bash
cd verse/src
make
```

This will compile both the server and client executables.

## Running

### Starting the server

```bash
./server
```

The server will start listening on port 7777 for incoming connections.

### Running the client

```bash
./client
```

The client will connect to the server at 127.0.0.1:7777, establish a NOISE session, and send a sample Fabric message.

## Protocol Details

The server implements the following functionality:

1. Accepts TCP connections on port 7777
2. Establishes a NOISE_XX encrypted session with each client
3. Processes encrypted Fabric messages with magic bytes `c0d3f33d`
4. Sends encrypted responses back to clients

The client:

1. Connects to the server
2. Establishes a NOISE_XX encrypted session
3. Sends a sample Fabric message with magic bytes `c0d3f33d`
4. Receives and processes the server's response

## Fabric Message Format

Fabric messages have the following format:

```
+----------------+----------------+----------------+
| Magic (4 bytes)| Type (4 bytes) | Payload (var) |
+----------------+----------------+----------------+
```

- **Magic**: 4 bytes (0xC0D3F33D) to identify Fabric messages
- **Type**: 4 bytes to specify the message type
- **Payload**: Variable-length payload data

## Notes on Noise-C API

The Noise-C library API requires:

1. Using `NOISE_DH_CURVE25519` for the Diffie-Hellman key exchange
2. Creating handshake states directly with protocol ID
3. Using the encrypt/decrypt functions that modify buffers in-place

For more information on the Noise-C library, see: [https://github.com/rweather/noise-c](https://github.com/rweather/noise-c) 