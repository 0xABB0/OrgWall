# quark

`Mel_Quark` — a `u32` handle that interns a `str8` to a stable, comparable identity.

## Why it exists

Code that keys on names (identifiers, paths, asset keys) wants integer-cheap equality and storage
without rehashing or re-comparing bytes at every site. Interning lives in its own library so any
module can carry quarks without dragging in the wider `string` surface, and so the intern table is
owned once rather than re-invented per consumer.

## Public surface

`<quark/quark.h>` — `Mel_Quark` (`MEL_QUARK_NONE` is the absent quark), `Mel_Quark_Table`
create/destroy over a `Mel_Alloc`, `mel_quark_intern` (copies the bytes, returns an existing quark
on a repeat), `mel_quark_lookup` (no insert), `mel_quark_get` (quark → owned `str8`), and
`mel_quark_count`.

## Model

Open-addressing table: linear probe over power-of-two buckets keyed by `str8_hash`, grown at 3/4
load. Quarks are 1-based dense indices into an entry array; `0` is reserved for `MEL_QUARK_NONE`.
The table owns each interned byte copy and frees it on destroy. Empty strings never intern.

## Dependencies

`core` (types), `allocator` (the table and byte copies are allocator-driven, defaulting to the heap
allocator when none is given), `collection` (`Mel_Array` entry store), `string` (`str8` and its
`hash`/`equals`/`is_empty` faces).
