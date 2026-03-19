# render.manager

Unified GPU-side object database. Stores per-object data in configurable storage pools. Bridges sources (who populate it) and pipelines (who draw from it). One manager type for both 2D and 3D — the pool layout determines the data shape, not the manager.

## Design

The manager is bytes. The shader interprets. The source writes whatever layout the material's shader expects. The pipeline binds the pool buffers and draws.

Custom sparse set with generational handles (`Mel_Render_Handle = {u32 idx, u32 gen}`). No slotmap dependency. Packed array with swap-on-free compaction.

Each object belongs to a **group** (assigned at alloc, changeable via `mel_mgr_change_group`). Groups determine draw ordering — the pipeline draws one group at a time, binding the appropriate GPU pipeline per group.

## Pools

Configurable at init. Each pool has a CPU data array, GPU buffer, and per-slot dirty bitset. Item size is fixed per pool, set at creation.

Convention (not enforced):
- 2D: pool 0 = transforms (32 bytes), pool 1 = render info (48 bytes)
- 3D: pool 0 = transforms (128 bytes), pool 1 = bounds (32 bytes), pool 2 = render info (32 bytes)

## Draw Order

A `draw_order` GPU buffer of u32 indices sorted by group via counting sort. Rebuilt when `order_dirty` is set (alloc, free, or group change). Per-group ranges (`Mel_Mgr_Range = {group, start, count}`) fall out directly from the prefix sums.

The shader reads through indirection: `uint slot = draw_order[push.draw_offset + SV_InstanceID]`. The `draw_offset` is passed via push constants because `SV_InstanceID` does not include Vulkan's `firstInstance`.

## API

```c
mel_mgr_init(mgr, .dev = dev, .pools = pool_descs, .pool_count = 2);
Mel_Render_Handle h = mel_mgr_alloc(mgr, group);
mel_mgr_set(mgr, pool_index, h, &data);
mel_mgr_upload_dirty(mgr);

const Mel_Mgr_Range* ranges = mel_mgr_group_ranges(mgr);
Mel_Gpu_Buffer* buf = mel_mgr_pool_buffer(mgr, pool_index);
Mel_Gpu_Buffer* order = mel_mgr_draw_order_buffer(mgr);
```
