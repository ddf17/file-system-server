# file-system-server

A Linux-based, epoll-driven file service server implemented in C.  
The server provides a simple TCP protocol supporting **LIST / GET / PUT / DELETE**
operations with non-blocking I/O and per-connection state machines.

This project is intended as a **systems programming portfolio project** focusing on
event-driven networking, robust I/O handling, and protocol correctness.

---

## Platform & Environment

**Linux only.**

- Uses `epoll` (`sys/epoll.h`) for event-driven, non-blocking I/O
- Tested on modern Linux distributions (e.g. Ubuntu 22.04+)
- Not supported on macOS / Windows (no epoll)

---

## Core Functionality

The server accepts multiple concurrent TCP clients and supports the following
file operations:

### 1. LIST
Returns the list of files currently stored on the server.

- Files are stored in a **temporary directory** created at server startup
- File names are separated by newline characters (`\n`)

### 2. GET `<filename>`
Downloads a file from the server.

- Server verifies file existence
- File size is sent before file data
- File data is streamed using non-blocking writes

### 3. PUT `<filename>`
Uploads a file to the server.

- Client sends file size followed by raw file bytes
- Server writes data incrementally to disk
- Partial or malformed uploads are rejected and cleaned up

### 4. DELETE `<filename>`
Deletes a file from the server.

- File must exist
- Server removes both on-disk file and in-memory metadata

---

## Networking & I/O Model

- **Non-blocking sockets** (`O_NONBLOCK`)
- Single-threaded **event loop** based on `epoll`
- Edge-triggered behavior (`EPOLLET`)
- Robust handling of:
  - `EINTR`
  - `EAGAIN` / `EWOULDBLOCK`
  - Partial reads and partial writes

The server does **not** block on disk or network I/O.

---

## Connection State Machine

Each client connection is represented by a `client_t` structure and progresses
through an explicit state machine:

