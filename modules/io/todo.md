# io todo

## win32 true async file I/O (pending win-pilot build)

The win32 backend opens files through the CRT (`_open`), which yields a
**synchronous** `HANDLE` (no `FILE_FLAG_OVERLAPPED`). The `port` proactor's win32
path issues `ReadFile`/`WriteFile` with an `OVERLAPPED`; on a synchronous handle
Windows completes inline and **blocks** the loop thread instead of returning
`ERROR_IO_PENDING`. Advertising `caps.async = true` there would be dishonest
(MEL-ENGINE-VIII), so the win32 backend reports `async_capable = false`:
`mel_stream_caps(s).async` is `false`, and `file_submit` always takes the
synchronous inline path on win32 (it blocks the caller, but never pretends to
defer).

To deliver true overlapped file I/O on win32:
- open with `CreateFileW(... FILE_FLAG_OVERLAPPED ...)` (drop the CRT fd, or wrap
  the overlapped `HANDLE` and keep a CRT fd only for the native-fd escape),
- bind the `HANDLE` to the reactor's IOCP,
- set `async_capable = true` once the handle is genuinely overlapped.

This requires compiling and running on `win-pilot` (host-only); it was not
attempted here because overlapped I/O cannot be verified without a win32 build.
