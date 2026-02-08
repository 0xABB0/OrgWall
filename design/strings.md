# String System Design

Status: **Blocked — Needs Design Decisions**

## Problem

The engine has `s8` (utf-8) and `s16` (utf-16) string types (data pointer + length). The types themselves are fine. The open questions are all about the *operations* on strings.

## Constraints

- Two string types: `s8` (utf-8) and `s16` (utf-16)
- Don't want to copy-paste method implementations for both types
- String operations may or may not allocate memory

## Open Questions

1. **Allocator for string operations**
   Operations like concat, format, substring-copy need to allocate. Which allocator?
   - Arena? Fast, no free needed, but caller must have an arena.
   - Mel_Alloc interface? Flexible, but then you lose the speed of specialized allocators.
   - Caller-provides-allocator per call? Explicit but verbose.
   - Arena-first with Mel_Alloc fallback?

2. **Code duplication across s8/s16**
   Both types have the same structure (pointer + length). Many operations are identical except for the element type (u8 vs u16). Options:
   - Macro-generate both implementations from a single template
   - Just write both (it's C, not that much code?)
   - Generic void* implementation with element size parameter (ugly, error-prone)

3. **lengthof macro safety**
   Current `s8(literal)` macro uses `lengthof()` which calls `countof()` — this silently does the wrong thing when passed a pointer instead of a literal. Needs a compile-time guard.

## Discussion

(No discussion yet — waiting for Gabbo to have a clearer picture of how he wants to handle the allocator question)

## Decision Log

(Nothing decided yet)
