# io todo

## win32 true async file I/O (pending win-pilot build)

The win32 backend opens files through the CRT (`_open`), which yields a
**synchronous** `HANDLE` (no `FILE_FLAG_OVERLAPPED`). The `port` proactor's win32
path issues `ReadFile`/`WriteFile` with an `OVERLAPPED`; on a synchronous handle
Windows completes inline and **blocks** the loop thread instead of returning
`ERROR_IO_PENDING`. Advertising `caps.async = true` there would be dishonest
(MEL-ENGINE-VIII), so the win32 backend reports `readiness = false`:
`mel_stream_caps(s).async` is `false`, and `file_submit` always takes the
synchronous inline path on win32 (it blocks the caller, but never pretends to
defer).

To deliver true overlapped file I/O on win32:
- open with `CreateFileW(... FILE_FLAG_OVERLAPPED ...)` (drop the CRT fd, or wrap
  the overlapped `HANDLE` and keep a CRT fd only for the native-fd escape),
- bind the `HANDLE` to the vat's IOCP waiter (wave 2 of the vat plan),
- mark the handle async once it is genuinely overlapped.

This requires compiling and running on `win-pilot` (host-only); it was not
attempted here because overlapped I/O cannot be verified without a win32 build.

## Regular-file executor offload (darwin/posix)

Regular files report `caps.async = false` and run the synchronous step inline:
readiness cannot represent a regular file (kqueue says always-ready; `read()` can
still block on a cold page), so the port path is reserved for fifo/socket/chardev
handles where readiness is physical. The doctrine's discipline for files is
executor offload — a worker does the blocking syscall and posts the completion
task to the vat (`mel_vat_post`), with `mel_vat_retain`/`release` bracketing the
in-flight window. Deferred from the vat-integration wave to keep it reviewable;
the readme records the design.
