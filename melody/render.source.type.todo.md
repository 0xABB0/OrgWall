# render.source.type

## TODO

- `Mel_Render_Source` lifetime does not remember which allocator created the source object, and `mel_render_source_destroy` always frees with `mel_alloc_heap()`. Store allocator ownership in the source so destruction matches creation.
- `create_manager` / `destroy_manager` currently push allocator responsibility out to each source implementation, and several implementations free with the heap allocator even when they were created with a different allocator. Tighten this contract so manager lifetime is allocator-safe by default.
