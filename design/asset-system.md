# Asset System Redesign

Status: **Not Started**

## Problem

The current asset registry and loading system has several issues:
- Bypasses the allocator system (raw malloc/realloc/free)
- No realloc failure checking
- Path strings may not be NUL-terminated
- Needs a proper data structure (map) for the registry
- The whole module needs to be rethought from the ground up

## Constraints

- Must use the allocator system (MEL-X-001)
- Needs a map/dictionary data structure (which doesn't exist yet)
- Should integrate with a future VFS (virtual file system)
- Should support hot-reloading (file watcher) eventually

## Open Questions

1. **Map data structure** — hash map? What hash function? Open addressing or chaining? This is a prerequisite.
2. **Asset lifetime management** — who owns loaded assets? Reference counting? Handle-based?
3. **VFS integration** — should the asset system go through a VFS layer, or load directly from disk?
4. **Async loading** — should asset loading be async from the start, or sync first and async later?

## Discussion

(No discussion yet)

## Decision Log

(Nothing decided yet)
