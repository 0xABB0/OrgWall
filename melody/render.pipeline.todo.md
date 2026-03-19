# render.pipeline

## TODO

- `Mel_Render_Pipeline` lifetime does not remember which allocator created the pipeline object, and `mel_pipeline_destroy` always frees with `mel_alloc_heap()`. Store allocator ownership in the pipeline so destruction matches creation.
