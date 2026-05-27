# Blender Undo, Serialization & DNA Study

Source: `~/repo/suck/blender/source/blender/blenkernel/`, `source/blender/blenloader/`, `source/blender/makesdna/`

## Undo System

### Architecture: hybrid command + snapshot

Vtable-dispatched step registry. Each undo type owns its own strategy.

### Core structures

```
UndoStack
  steps (doubly-linked list)
  step_active (cursor)
  step_active_memfile (last loaded global snapshot)

UndoType (vtable)
  poll()
  step_encode_init() (optional)
  step_encode()
  step_decode()
  step_free()
  step_foreach_ID_ref() (remap data-block pointers)

UndoStep (base, over-allocated to type->step_size)
  name, type, data_size, skip, use_memfile_step, is_applied
```

### Undo type resolution

`BKE_undosys_type_from_context(C)` iterates `g_undo_types`, calls `poll()` on each. First match wins. Priority: sculpt > image > mesh edit > curve edit > ... > global memfile (catch-all).

### How push works

1. Truncate future steps (linear history, no tree)
2. If incoming type uses ID refs and no global snapshot since last step, force-write one (marked `skip=true`)
3. Allocate `UndoStep` of `ut->step_size` (over-allocated struct)
4. Call `step_encode`
5. If `use_memfile_step` (e.g. mode switch), push additional memfile step

### Global undo (memfile)

`step_encode` calls `BLO_write_file_mem` -- writes entire Blender file into `MemFile` (linked list of chunks). Previous step's MemFile used for chunk deduplication.

`step_decode` calls `BLO_read_from_memfile` -- reads back into new Main. `use_old_bmain_data` optimization reuses unchanged IDs.

On free: `BLO_memfile_merge` transfers chunk ownership (identical chunks shared, no double-free).

### Mode-specific undo (e.g. mesh edit)

Stores per-mesh BMesh snapshots using `BLI_array_store` -- deduplicating block-level array store with RLE + chunk dedup. Only changed chunks stored.

### ID pointer stability across undo

Problem: local undo steps reference IDs by pointer. After global undo, Main reconstructs and all pointers change.

Solution: `step_foreach_ID_ref` with `UndoRefID`:
- Before storing: copy `ID->name` + library filepath, null pointer
- Before restoring: ensure correct memfile loaded, resolve by name lookup in new Main

## .blend File Format

### Structure: block-based binary with embedded schema

Flat sequence of `BHead` blocks:

```
BHead {
  int   code;      // DATA, GLOB, DNA1, USER, ENDB, or ID code (OB, ME...)
  int   SDNAnr;    // SDNA struct index
  void* old;       // original memory address (for pointer fixup)
  int64_t len;     // byte count
  int64_t nr;      // element count
}
```

Three wire formats: BHead4 (32-bit), SmallBHead8 (64-bit, len <= 2GB), LargeBHead8.

Notable codes: `DNA1` = SDNA block (entire schema), `ENDB` = EOF.

### Write/Read API

Each ID type implements:
- `blend_write(BlendWriter*)`: calls `writer->write_struct<T>()`, etc.
- `blend_read_data(BlendDataReader*)`: fixes up serialized pointers
- `blend_read_lib(BlendLibReader*)`: fixes up cross-ID pointers

Not reflection-based -- each type explicitly controls its own serialization.

### Pointer fixup on load

`FileData` maintains `OldNewMap` -- hash table of old address -> new address. `BLO_read_struct` looks up old pointer, returns new. Unserializable pointers (runtime caches) become null.

## The DNA/SDNA System

The legendary part. Runtime struct introspection + forward/backward compatibility.

### Build-time: makesdna

Standalone C++ program that:
1. Parses all `DNA_*.h` headers
2. Extracts struct definitions (types, members, sizes, arrays)
3. Emits `dna.c` containing `const unsigned char DNAstr[]` -- binary schema

Binary format:
```
"SDNA" magic
"NAME" <count> member name strings ("*next", "co[3]", etc.)
"TYPE" <count> type name strings
"TLEN" <count> type sizes (short[])
"STRC" <count> struct definitions: [type_idx, member_count, (type_idx, name_idx)...]
```

Pointer and array encoded in name string: `"*next"` = pointer, `"co[3]"` = array of 3.

### Runtime: SDNA struct

```
SDNA {
  const char *data;
  int pointer_size;         // 4 or 8 (inferred from ListBase size)
  int types_num;
  const char **types;
  short *types_size;
  int structs_num;
  SDNA_Struct **structs;
  GHash *types_to_structs_map;
  struct { ... } alias;     // rename mappings
}
```

On load: file's embedded SDNA = `oldsdna`, running Blender's SDNA = `newsdna`. Both coexist.

### Struct comparison

For each struct in oldsdna, compares against newsdna:
- `SDNA_CMP_EQUAL` -- memcpy directly
- `SDNA_CMP_NOT_EQUAL` -- field-by-field reconstruction
- `SDNA_CMP_REMOVED` -- struct no longer exists

Comparison checks: existence, member count, total size, each member's type+name, pointer size, embedded sub-structs (recursive).

### Struct reconstruction

For `NOT_EQUAL` structs:
1. Allocate zeroed buffer (new layout, new fields default to zero)
2. For each member in new struct, find matching by name in old
3. Same type: memcpy
4. Different primitive: `cast_primitive_type()` converts (including char->float /255 for colors)
5. Not found: stays zero (new field)

Handles: field reordering, type changes, array size changes, added/removed fields, 32-to-64-bit pointer conversion.

### The rename system

`dna_rename_defs.h`:
```c
DNA_STRUCT_RENAME(Lamp, Light)
DNA_STRUCT_RENAME_MEMBER(Object, col, color)
```

In `makesdna.cc`: writes new name into SDNA.
In `dna_genfile.cc`: populates alias tables mapping old -> new names.

Result: file with `Lamp` loads transparently into code expecting `Light`.

### Pointer size detection

Inferred from `ListBase` size in file's SDNA (always exactly two pointers). No trust issues.

## Asset System

Catalog + representation layer on top of .blend infrastructure.

- `AssetLibrary` owns `AssetCatalogService` (tree of paths, .csv sidecar files) + `AssetStorage` (set of `AssetRepresentation`)
- `AssetRepresentation` -- lightweight proxy (name, ID type, metadata). Not loaded until needed.
- `AssetCatalogService` builds in-memory tree from `blender_assets.cats.txt` (UUID/path/name)
- `AssetWeakReference` -- stable cross-file reference (library name + relative path + ID group + name)

## Key Patterns

- **Hybrid undo**: vtable-dispatched, mode-specific local deltas + global memfile snapshots
- **MemFile chunk deduplication**: identical chunks shared across undo steps, ownership transfers on merge
- **DNA struct introspection**: build-time schema extraction, embedded in files, field-by-field reconstruction on version mismatch
- **Rename macros**: compile-time declarations that bridge old/new names transparently
- **Block-based .blend**: self-describing via embedded SDNA, pointer fixup via old->new address map
- **ID pointer stability**: name-based resolution across undo boundaries
