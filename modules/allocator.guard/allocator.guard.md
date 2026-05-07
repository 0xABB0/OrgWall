# allocator.guard

`allocator.guard` is a debugging allocator decorator.

It wraps another `Mel_Alloc` and adds corruption detection without changing the caller-facing allocation API.

## Goals

- catch heap corruption early
- make use-after-free louder
- make large or sampled allocations fail fast with virtual-memory protection
- keep release cost at zero unless explicitly requested

## Features

Depending on configuration, the guard allocator can provide:

- head canaries
- tail canaries
- poison-on-alloc
- poison-on-free
- quarantine-on-free
- page-protected allocations backed by `allocator.vmem`

## Important honesty

Page protection is expensive and cannot perfectly catch every tiny underrun and overrun for every allocation without absurd cost.

So the design is layered:

- canaries catch adjacent corruption
- poison helps reveal uninitialized/use-after-free reads in debugging
- quarantine delays reuse and helps catch stale writes
- page protection is used for selected allocations where immediate trapping is worth the cost

## Intended usage

`allocator.guard` is the primitive.

The engine-level policy is allowed to install it as the default heap allocator through compile-time configuration so even pre-`main` allocations are covered.

## Contracts

- `mel_guard_init` does not heap-allocate internal metadata
- `mel_guard_shutdown` requires no live allocations
- `realloc` is implemented as allocate-copy-free for correctness and clarity
- corruption is a hard failure
