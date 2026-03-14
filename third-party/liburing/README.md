# liburing (vendored)

Vendored from: https://github.com/axboe/liburing
Commit: 34666813779a4196361d73b1982a9baadb355528
Date: 2026-03-13
License: MIT (see LICENSE)

Linux-only. Provides the userspace API for io_uring.

## What's included

- `src/include/liburing.h` — main header
- `src/include/liburing/` — supporting headers (barrier, io_uring, sanitize)
- `src/setup.c`, `src/queue.c`, `src/register.c`, `src/syscall.c` — core implementation
- `src/arch/` — architecture-specific syscall definitions (x86, aarch64, riscv64, generic)
- `src/lib.h`, `src/setup.h`, `src/syscall.h`, `src/int_flags.h` — internal headers
- `src/nolibc.c`, `src/sanitize.c`, `src/ffi.c`, `src/version.c` — additional source files

## Not included

Tests, examples, man pages, spec files, build system (Makefile/configure), linker scripts.
