# File System Server

A cross-platform file storage service written in C. It implements a small
client-server protocol for uploading, downloading, listing, and deleting files
over TCP on Linux and macOS.

Think of it as a systems-programming version of the core idea behind products
like Google Drive, Dropbox, or Baidu Netdisk: clients talk to a remote service
that stores files and lets users retrieve or manage them later. This project is
not trying to be a production cloud drive. Instead, it focuses on the lower-level
engineering pieces that make this kind of service work: socket programming,
protocol design, non-blocking I/O, connection state management, and robust file
transfer behavior.

## What This Project Does

The project contains two command-line programs:

- `file-server`: starts a TCP server and stores uploaded files in a temporary
  directory.
- `file-client`: connects to the server and performs one file operation.

The supported operations are:

| Operation | Description |
| --- | --- |
| `LIST` | Show all files currently stored on the server. |
| `GET <remote> <local>` | Download a server-side file and save it locally. |
| `PUT <remote> <local>` | Upload a local file and store it under a server-side name. |
| `DELETE <remote>` | Delete a file from the server. |

Each client request opens a TCP connection, sends exactly one command, reads the
server response, and then exits.

## Why It Is Interesting

The server is designed around a single-threaded event loop instead of one thread
per client. That means one process can keep track of many active connections and
make progress whenever a socket becomes readable or writable.

Key implementation points:

- Uses OS-level event notification for scalable I/O.
- Uses `epoll` on Linux and `kqueue` on macOS.
- Uses non-blocking sockets so slow clients do not stall the whole server.
- Stores per-client progress in an explicit connection state machine.
- Handles partial reads and writes instead of assuming one system call transfers
  everything.
- Streams file contents in fixed-size chunks so large files do not need to fit in
  memory.
- Cleans up incomplete uploads and temporary server storage on exit.

## Protocol Overview

The protocol is intentionally small. Requests and responses start with a text
line, followed by binary data only when needed.

Client request:

```text
COMMAND [filename]\n
[file size][file bytes]
```

Server response:

```text
OK\n
[file size][file bytes]
```

or:

```text
ERROR\n
[error message]\n
```

File sizes are sent as raw `size_t` bytes. `PUT` requests include a file size and
file data from the client. `GET` and `LIST` responses include a file size and
data from the server.

## Architecture

At a high level, the server works like this:

```text
start server
  create temporary storage directory
  open listening socket
  register socket with the platform event system
  wait for events
    accept new clients
    read request headers
    receive uploaded file data
    send status responses
    stream downloaded file data
    close completed connections
```

Each connected client has its own state object. The server keeps those objects in
a file-descriptor map so a socket event can be routed back to the correct
request.

The event system is intended to be platform-aware:

```text
Linux   -> epoll
macOS   -> kqueue
server  -> shared protocol and file-transfer state machine
```

That keeps the core server logic focused on the file protocol while the
operating-system-specific event mechanism stays behind a small boundary.

## Build

This project is being updated to support both Linux and macOS.

```bash
make
```

The build creates:

```text
bin/file-server
bin/file-client
```

The intended platform behavior is:

- Linux builds use `epoll`.
- macOS builds use `kqueue`.
- Windows is not supported.

The original course version targeted Linux `epoll`; this cleaned-up version is
being adapted so it can also run natively on macOS.

## Run

Start the server:

```bash
./bin/file-server 9001
```

In another terminal, run client commands:

```bash
./bin/file-client 127.0.0.1:9001 PUT notes.txt ./notes.txt
./bin/file-client 127.0.0.1:9001 LIST
./bin/file-client 127.0.0.1:9001 GET notes.txt ./downloaded-notes.txt
./bin/file-client 127.0.0.1:9001 DELETE notes.txt
```

## Repository Layout

```text
src/
  client.c      command-line client
  server.c      event-driven file server
  common.c      shared socket and file helpers
  format.c      shared user-facing messages

include/fss/
  common.h
  format.h
  vector.h
  dictionary.h
  ...

docs/
  design.md     implementation design notes
```

## Project Scope

This is a learning project, not a production cloud storage system. It does not
include authentication, encryption, persistent storage, directory trees, access
control, resumable uploads, or replication. The goal is to make the networking
and file-transfer core clear, small, and inspectable.
