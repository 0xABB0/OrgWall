# process — todo

- Zero-latency exit detection: replace the deadline-poll `waitpid(WNOHANG)` reap
  with a kqueue `EVFILT_PROC` (apple) / `pidfd` (linux) wakeable once the vat
  waiter accepts non-fd filters (today `Mel_Vat_Wakeable.events` speaks only
  IN/OUT/ERR/HUP, so a pid cannot be armed).
- win32 backend is built/run remotely on win-pilot; not host-verified here.
- pty/tty stdio allocation.
