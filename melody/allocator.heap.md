# allocator.heap

`allocator.heap` is the engine's default heap allocator.

In normal builds it wraps the system heap.

When `MEL_MEMORY_DEBUG` is enabled, it becomes the global policy boundary that returns a guarded allocator instead. This lets the entire engine, including allocations that happen before `main`, run through the memory-debug layer.

## Compile-time policy

- `MEL_MEMORY_DEBUG_NONE`
- `MEL_MEMORY_DEBUG_LIGHT`
- `MEL_MEMORY_DEBUG_HEAVY`
- `MEL_MEMORY_DEBUG_PARANOID`

## Guarantees

- `mel_alloc_heap()` is stable to call from constructors and other early-init code
- aligned allocation requests are honored
- memory-debug behavior is explicit at compile time
- release builds pay nothing unless configured to do so
