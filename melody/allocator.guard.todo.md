# allocator.guard todo

- improve corruption reports with richer callsite and pointer-range details
- add optional immediate quarantine sweeping hooks for long-idle programs
- add explicit double-free detection marker instead of relying only on prefix/header corruption
- add optional statistical counters for guard hits and quarantine evictions
- revisit composition with `allocator.tracking` and `allocator.leak` so decorators can be stacked cleanly
- consider alternating protected allocation bias directions for sampled allocations to distribute overrun/underrun coverage more evenly
