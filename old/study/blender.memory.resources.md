# Blender Memory & Resource Management Study

Source: `~/repo/suck/blender/intern/guardedalloc/` and `source/blender/blenkernel/`

## Memory Allocators (guardedalloc)

### Two implementations behind function pointers

**Lockfree allocator** (production): wraps `malloc` with minimal `MemHead` prefix (`size_t len` + 2 flag bits). Tracks total bytes and block count with atomics. Fast.

**Guarded allocator** (debug): full `MemHead` with `tag1/tag2` sentinels (magic `MEMO`/`RYBL`), doubly-linked list, allocation `name` (static string pointer, not copy), optional backtrace. `MemTail` with `tag3` (`OCK!`) follows user data. Checks all sentinels on free for overflow detection. Global linked list protected by mutex.

`MEM_use_lockfree_allocator()` / `MEM_use_guarded_allocator()` flips function pointers. Switch only before first allocation.

### C++ API

- `MEM_new<T>(name, args...)` -- allocate + construct. Tracks `DestructorType` flag.
- `MEM_new_zeroed<T>` / `MEM_new_uninitialized<T>` -- trivial types only
- `MEM_delete<T>` checks destructor flag

### Leak Detection

`MEM_init_memleak_detection()` installs static `MemLeakPrinter`. At exit, walks guarded allocator's linked list and reports all live allocations. `MEM_enable_fail_on_memleak()` for CI.

Names are **static string pointers** -- must outlive the allocation.

## The ID System (Data-blocks)

### The ID struct -- universal header

Every serializable data-block starts with `ID` as first member (C inheritance):

```c
struct ID {
  void *next, *prev;       // linked list in Main's per-type list
  ID   *newid;             // used during copy
  Library *lib;            // null = local, non-null = linked
  char  name[258];         // first 2 bytes = type code ("ME", "OB")
  short flag;              // persistent (ID_FLAG_FAKEUSER)
  int   tag;               // runtime-only (ID_TAG_NO_MAIN, ID_TAG_COPIED_ON_EVAL)
  int   us;                // user (reference) count
  unsigned int session_uid;
  ID   *orig_id;           // for COW copies: back to original
};
```

### Reference Counting

`ID.us` is plain int, not atomic. Main thread only. Manipulated through `id_us_plus()` / `id_us_min()`.

"Fake user" (`ID_FLAG_FAKEUSER`) increments `us` by 1 to keep alive with no real references.

### IDTypeInfo -- type vtable

```cpp
struct IDTypeInfo {
  short id_code;
  size_t struct_size;
  IDTypeInitDataFunction init_data;
  IDTypeCopyDataFunction copy_data;
  IDTypeFreeDataFunction free_data;
  IDTypeForeachIDFunction foreach_id;  // walks all ID pointers
  IDTypeBlendWriteFunction blend_write;
  IDTypeBlendReadDataFunction blend_read_data;
  // ...
};
```

`foreach_id` is the **spine of correctness** -- remapping, user counting, dependency graph all depend on it.

### Lifecycle

- **Create**: `BKE_id_new` -> `BKE_libblock_alloc()` -> sized alloc + init. `us = 1`.
- **Copy**: `BKE_libblock_copy_ex` with flag bitmask (bypass Main, skip refcount, use pre-allocated buffer, mark COW).
- **Delete**: `id_free()` -> free data + free datablock + remove from Main + MEM_delete.

### Main Database

`Main` holds one `ListBase` per ID type. IDs addressed by name.

## Copy-on-Eval (Depsgraph COW)

### Two worlds: original vs evaluated

Each `IDNode` owns `id_orig` (original in Main) and `id_cow` (evaluated copy). `ID::orig_id` on eval copy points back.

### The copy process

1. At build time: eval ID pre-allocated, `name[0]` zeroed (sentinel for unexpanded)
2. At eval: `deg_expand_eval_copy_datablock()` calls `id_copy_inplace_no_main()` -- deep copy into pre-allocated buffer, no Main, no user count adjustment
3. Pointer remapping: `BKE_library_foreach_ID_link()` replaces every original pointer with its evaluated counterpart
4. Edit-mode pointers are **shared** back to original (not duplicated)

### Update cycle

1. Save runtime data (sculpt session, caches)
2. Free old eval copy
3. Re-expand from (modified) original
4. Restore runtime data

## CustomData -- Per-Element Attributes

```c
struct CustomData {
  CustomDataLayer *layers;
  int typemap[CD_NUMTYPES];  // type -> first layer index (O(1))
  int totlayer, maxlayer;
};

struct CustomDataLayer {
  int type;
  char name[68];
  void *data;                           // contiguous array
  const ImplicitSharingInfoHandle *sharing_info;  // refcount COW
};
```

### LayerTypeInfo -- attribute type vtable

Each of 53 types has: `size`, `alignment`, `copy`, `free`, `interp` (weighted interpolation for subdivision), `construct`, `set_default_value`, `validate`.

### Implicit Sharing

`ImplicitSharingInfo`: abstract base with atomic `strong_users_` / `weak_users_`.
- `is_mutable()` returns true iff `strong_users_ == 1`
- Read: `CustomData_get_layer()` -- O(1) via typemap
- Write: `CustomData_get_layer_for_write()` -- triggers COW unshare if needed

## Key Patterns

- **Two distinct COW systems**: depsgraph (ID-level), ImplicitSharingInfo (attribute-array level)
- **Ownership is explicit but multi-layered**: Main owns IDs (user count), depsgraph owns eval copies, ImplicitSharing owns attribute arrays
- **`foreach_id` callback is critical**: every ID type must implement it for remapping/counting to work
- **Allocator names are static pointers**: no string copies, must outlive allocation
- **Swappable allocator**: debug vs production, chosen at startup via function pointer table
