# process — todo

- Zero-latency exit detection: replace the reactor-timer `waitpid(WNOHANG)` reap
  with a kqueue `EVFILT_PROC` (apple) / `pidfd` (linux) reactor source behind the
  backend seam once the reactor surfaces a process-readiness source.
- win32 backend is built/run remotely on win-pilot; not host-verified here.
- pty/tty stdio allocation.
