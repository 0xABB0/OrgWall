# render.source.manual

## TODO

- `mel_source_manual_add(...)` still requires the source to already be attached to a scene because it immediately allocates instance and space handles from the scene manager. Support staging before attach, or introduce an explicit authored-object layer for manual sources.
- `Mel_Render_Info` is still a legacy single-mesh / single-binding entrypoint. The richer model now lives in `mel_source_manual_set_mesh_parts(...)` and `mel_source_manual_set_material_bindings(...)`. Replace the one-mesh bootstrap shape with a cleaner explicit add descriptor.
- Mesh parts currently reference whole `Mel_Geometry_Handle`s. There is no way yet to express submesh/index ranges inside one geometry handle, so complex authored meshes still need to be pre-split at upload time.
