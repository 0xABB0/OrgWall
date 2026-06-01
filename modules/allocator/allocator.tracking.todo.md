# allocator.tracking todo

- consumer-side reporter that routes `Mel_Track_Report_Cb`/`Mel_Track_Bucket_Cb`
  to `mel_log_*`, living in a layer allowed to depend on `log` + `debug`
- backtrace symbolication helper (resolve raw frames via `debug` stacktrace) at report time
- snapshot/diff API: capture two `Mel_Track_Allocator_Stats` and diff for per-frame deltas
- assert `meta` is not the tracker's own interface once the iface is known
- replace libc `assert` with `mel_assert` when `mel_assert` stops being a no-op
