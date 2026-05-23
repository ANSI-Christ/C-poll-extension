# С poll extеnsion

**poll_ext** is a lightweight, portable C89 helper for building asynchronous socket state machines.
It takes care of waiting for socket readiness and notifying via a callback.
It doesn’t hide OS differences, doesn’t force any threading model, and stays out of your way.

## Features

- **Minimal overhead** — just event arrays, no extra allocations
- **Minimal dependencies** - socket OS headers + libc
- **Cross-platform without abstractions** — native sockets and error codes (`E…` / `WSAE…`)
- **Thread-safe registration** — add sockets from any thread via a single control channel
- **Multiple reactors support** — run several `poll_loop` instances in different threads
- **Per-operation timeouts** — each operation can have an independent deadline
- **Useful helpers** - manual implementations of byte swapping and DNS RFC 1035 request / response
- **Simplicity** — ~600 lines of code, easy to integrate

## Link with flags:
 - Windows: -lws2_32
 - Solaris: -lsocket

## Notes
 - examples are in [main.c](main.c)
 - on Windows if `WSApoll()` isn't exist then have auto fallback to `select()`
 - on UNIX if `poll()` isn't exist then it have fallback to select with -DPOLL_BY_SELECT flag
