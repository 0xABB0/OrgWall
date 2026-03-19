# render.source.manual

## TODO

- `mel_source_manual_add` requires `source->manager != nullptr`, which means a manual source cannot be populated until some view has already attached it and forced manager creation. Allow staging objects before the first view uses the source, or expose an explicit source init path so gameplay code is not coupled to view creation order.
- Manager destruction currently frees with `mel_alloc_heap()` even though creation uses the allocator passed into the source. Fix allocator ownership so manual sources destroy their manager with the same allocator that created it.
