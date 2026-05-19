# Design Notes

This project is a small remote file storage service. A client connects to the
server, asks for one file operation, receives a response, and then disconnects.
The server keeps running and handles many clients through one event-driven
process.

The design is intentionally close to the way a larger cloud storage service is
structured, but with most production concerns removed. There are no users,
permissions, encryption, directories, replication, or persistent databases. The
focus is the core file-transfer path.

## System Shape

```text
command-line client
        |
        | TCP request
        v
file storage server
        |
        | reads and writes files
        v
temporary server directory
```

The client is short-lived. It sends one command such as upload, download, list,
or delete, then exits.

The server is long-lived. It owns the listening socket, accepts clients, stores
files in a temporary directory, and keeps track of active connections.

## Platform Event Layer

The server is designed around operating-system event notification instead of one
thread per client.

```text
Linux socket events   -> epoll
macOS socket events   -> kqueue
server logic          -> shared connection state machine
```

The important design goal is that the file protocol should not care which
operating system is running underneath. Linux and macOS expose different APIs for
watching sockets, but the server only needs a few concepts:

- create an event queue
- watch a socket for readable data
- watch a socket for writable space
- stop watching a socket when the request is finished
- wait until one or more sockets are ready

Keeping that boundary small makes the rest of the server easier to read. The
request parser, file upload logic, file download logic, and cleanup rules can be
shared across platforms.

## Request Lifecycle

Every client connection moves through a simple lifecycle:

```text
new connection
  read request line
  understand requested operation
  perform file operation
  send response
  close connection
```

For upload requests, the server also receives a file size and then streams the
file body into the temporary storage directory.

For download and list requests, the server sends a file size and then streams the
response body back to the client.

## Connection State Machine

Each connected client has a small state object. That object records what the
server is currently doing for the connection, how much data has already been
read or written, and which file is involved.

The states are named after the work being performed:

```text
read request header
  |
  | upload command
  v
read upload size
  |
  v
read upload data
  |
  v
send success response
  |
  v
close connection
```

```text
read request header
  |
  | download command
  v
send success response
  |
  v
send file size
  |
  v
send file data
  |
  v
close connection
```

```text
read request header
  |
  | list command
  v
send success response
  |
  v
send file list
  |
  v
close connection
```

```text
read request header
  |
  | delete command
  v
delete file
  |
  v
send success response
  |
  v
close connection
```

If anything goes wrong, the connection switches to an error response path:

```text
detect error
  |
  v
send error response
  |
  v
close connection
```

This structure is useful because non-blocking I/O often does only part of the
work in one system call. For example, a socket may accept only half of a response
right now. The state object lets the server remember where to resume when the
socket becomes writable again.

## Core Data Structures

The server keeps three main pieces of shared state:

| Data | Purpose |
| --- | --- |
| Temporary directory path | Location where uploaded files are stored while the server is running. |
| File name list | In-memory list used to answer list requests and check delete/download targets. |
| Connection map | Maps each socket file descriptor to its active connection state. |

Each connection state stores:

- socket file descriptor
- current lifecycle state
- requested operation
- request header buffer
- remote filename
- open file descriptor, if a file is being read or written
- expected file size
- number of bytes already transferred
- response buffer and current write offset

## File Storage

The server creates one temporary directory when it starts. Uploaded files are
stored there using the remote filename provided by the client.

The storage model is deliberately simple:

- Uploading an existing filename replaces the old file.
- Deleting a file removes it from disk and from the in-memory file list.
- Downloading a file streams bytes from disk to the client.
- Listing files builds a newline-separated response from the in-memory file list.
- Server shutdown removes the temporary files and the temporary directory.

Because the directory is temporary, the server does not preserve files after it
exits.

## Protocol Flow

The client sends a text command first:

```text
COMMAND [filename]\n
```

Only upload requests include data after the command line:

```text
[file size][file bytes]
```

The server replies with either success:

```text
OK\n
```

or failure:

```text
ERROR\n
[message]\n
```

Download and list success responses include a size followed by response data:

```text
OK\n
[response size][response bytes]
```

The size field is a raw `size_t`. Both sides read and write the exact number of
bytes described by that value.

## Error Handling

The server treats the network as untrusted. A client may disconnect early, send a
malformed request, send too few file bytes, or send too many file bytes.

Important error rules:

- Unknown or malformed commands return a bad request error.
- Downloading or deleting a missing file returns a no-such-file error.
- Uploads with the wrong amount of file data return a bad-file-size error.
- Failed uploads are removed so incomplete files are not kept.
- Broken sockets are closed and removed from the connection map.
- Writes to disconnected clients are handled without crashing the server.

The client also validates server responses. If the server promises a certain
amount of data but sends less or more, the client reports that mismatch.

## Why Non-Blocking I/O Matters

A blocking server can get stuck waiting for one slow client. This server avoids
that by putting sockets into non-blocking mode and only doing work when the
operating system reports that a socket is ready.

That makes the server more scalable without adding threads:

```text
one process
one event loop
many active clients
small amount of work per ready socket
```

The tradeoff is that the code must carefully remember partial progress. That is
why the connection state machine is the center of the server design.
