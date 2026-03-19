# render.manager

## TODO

- MEL_MATERIAL_BASE_MAX (32) stack arrays used in counting sort (`mel__mgr_rebuild_draw_order`). `u32 counts[32]`, `u32 offsets[32]`, `u32 write_pos[32]` on the stack. Violates MEL-X-004 (no static-size buffers). Should dynamically allocate based on actual max group ID. The material_base module itself also uses `s_bases[MEL_MATERIAL_BASE_MAX]` — same underlying issue.
- Storage buffers are always allocated as `MEL_GPU_MEMORY_USAGE_CPU_TO_GPU`, including draw order. This bakes one storage policy into the manager and prevents GPU-local placement for GPU-only pipelines. Split "CPU-side mirror + upload path" from "GPU allocation policy" so the manager can choose GPU-local storage when the active pipelines do not need CPU-visible buffers.
