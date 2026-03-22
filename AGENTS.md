# CLAUDE.md

This file provides guidance to agents when working with code in this repository.

## Rules

- Follow @vision.md — vision and commandments take priority over everything else.
- Follow @RULES.md — practical code style and engine rules.
- No stubs, no hacks, no "not implemented". If we lack something, we implement it.
- Negotiate the interface with gabbo before designing a new module.

## Build

```bash
cc -o nob nob.c   # bootstrap (once)
./nob              # build
./nob test         # run tests (see @TESTING.md)
```

## Reference

- @vision.md — what this engine is, why it exists, and the commandments
- @RULES.md — practical code style and engine rules
- @STRUCTURE.md — file naming, types, include rules, directory layout
- @MODULES.md — module listing
- @TESTING.md — writing and running tests
- @DESIGN_REVIEW.md — design review checklist (used when reviewing or writing design docs)
- `design/` — feature design documents

## About this repo

Heavily WIP. No backwards compatibility concerns. Destructive changes are fine.

Keep docs updated. Holes in implementation go in todo.md.

The engine grows by implementing things and addressing pain points — fix them or document them in the todo file.
