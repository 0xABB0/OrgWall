# render.source.ecs.2d

## TODO

- Manager destruction currently frees with `mel_alloc_heap()` even though creation uses the allocator passed into the source. Fix allocator ownership so 2D ECS sources destroy their manager with the same allocator that created it.
