# render.material_base

## TODO

- Material instance IDs are unstable. `mel_material_base_free_instance` compacts by moving the last params block into the freed slot, but render objects keep raw `material_idx` values and nothing remaps those references. Either make instance handles stable or add a remap/update path for every user of material indices.
- `s_bases[MEL_MATERIAL_BASE_MAX]` is a fixed-size registry. This violates MEL-X-004 and should become a dynamic structure.
