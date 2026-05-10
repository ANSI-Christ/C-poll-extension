# С poll extеnsion

**poll_ext** is a minimalistic wrapper around the system `poll()`/`WSAPoll()` for asynchronous socket operations.

The library doesn't hide OS specifics or impose a usage model — you get native sockets and error codes, but with convenient callback registration and timeouts.

**Why:** Enables writing async code in a finite-state-machine style where each step (connect/send/recv) doesn't block the thread but looks linear.

## Features

- **Minimal overhead** — just event arrays, no extra allocations
- **Cross-platform without abstractions** — native sockets and error codes (Linux/Windows/macOS/WSL)
- **Thread-safe registration** — add sockets from any thread via a single control channel
- **Per-operation timeouts** — built-in `_tm` versions
- **Multiple reactors support** — run several `poll_loop` instances in different threads
- **Simplicity** — ~350 lines of code, easy to integrate
- **Async code linearization** — FSM looks synchronous but doesn't block

## Link with flags:
 - Windows: -lws2_32
 - Solaris: -lsocket

## Notes
 - examples are in [main.c](main.c)
