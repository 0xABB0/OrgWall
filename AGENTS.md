# CLAUDE.md

This file provides guidance to agents when working with code in this repository.

## Rules:

- Follow the Guidebook. Treat it as a bible. @GUIDEBOOK.md
- never, ever, write code as not implemented, stubs, hacks to make things work or make crooks. if we cannot implement something because we're lacking some stuff, we implement it.
- Before designing a new module, you need to negotiate the interface with gabbo.

## Build Commands

The project uses nob. Bootstrap it first:
```bash
cc -o nob nob.c
```

Then use:
```bash
./nob
```

Testing: `./nob test` (see @TESTING.md for details)

## Vision for this codebase:

it's EXTREMELY IMPORTANT to follow the vision for this codebase. it takes 100% priority over anything else.

@vision.md

## Reference docs

- @STRUCTURE.md — file naming, types, include rules, directory layout
- @MODULES.md — module listing, logging conventions
- @TESTING.md — writing and running tests
- @DESIGN_REVIEW.md — design review checklist
- @GUIDEBOOK.md — routing file for commandments + rules
- @COMMANDMENTS.md — philosophical foundation (programmer + engine commandments)
- @RULES.md — practical code style and engine rules
- `design/` folder — feature design documents

## Warning:

This repo is HEAVILY WIP. stuff is not how it should actually be. our job, other than moving this project forward, is also to align this project with changing guidelines.
this CLAUDE.md file, the guidebook and the todo files should give the guidelines.

Since this repo is heavily wip, we have no need to care for backwards compatibility when making changes, and we can make destructive changes

## Good practices

We should strive to keep documentation files updated, during discussions we should also strive to stick (and update) the GUIDEBOOK.md.
It's ok to leave holes in the implementation, as long as they are put inside todo.md

## Engine growth

The engine grows by implementing stuff and pinpointing the pain points. Everytime we implement something, we should thoroughly address pain points (either by fixing them, or putting this in the todo file)
