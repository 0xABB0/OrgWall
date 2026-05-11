# Melody Engine

## Rules

- @vision.md is the highest priority. Vision and commandments override everything.
- @RULES.md for practical code style and engine rules.
- No stubs, no hacks, no "not implemented". If we lack something, we implement it.
- Negotiate the interface with gabbo before designing a new module.

## Build

```bash
cc -o nob nob.c   # bootstrap (once)
./nob              # build
./nob test         # run tests
```

## Reference

- @STRUCTURE.md — file naming, types, include rules, directory layout
- @MODULES.md — module listing
- @TESTING.md — writing and running tests
- @DESIGN_REVIEW.md — design review checklist
- `design/` — feature design documents

## About this repo

Heavily WIP. No backwards compatibility concerns. Destructive changes are fine.
Keep docs updated. Gaps in implementation go in todo.md — fix problems or document them.
