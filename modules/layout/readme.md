# layout

Layout solvers, extracted from `gui`. Pure math over item arrays: the host
(gui, the drawn family, a test) fills `Mel_Layout_Item` inputs from its own
tree, the solver writes x/y/w/h outputs, the host applies them. The module
never walks a host tree, never allocates, and has no gui dependency.

## The class model

A layout kind is its `Mel_Layout_Class` pointer — an open set, never a tag.
This is what makes OS-layout lowering possible: a gui backend that recognises
`mel_linear_layout_class()` reads the public `Mel_Linear_Layout` fields and
hands arrangement to the platform engine (CSS flex on web, `LinearLayout` on
Android); a backend that doesn't recognise a class — or a platform whose idiom
is absolute positioning (win32) — runs `measure`/`arrange` portably and pushes
bounds. Same description, two honest executions. A custom class an app defines
out-of-tree is always solved portably.

## Kinds

- `linear` (`<layout/linear.h>`) — one axis-parametric kind serving column
  (vertical) and row (horizontal): spacing, container margin, per-child
  margins, weights over leftover space, cross-axis align (start/center/end/
  stretch).
- `stack` (`<layout/stack.h>`) — overlay; every child fills the container
  minus margins. Per-child alignment within the overlay is deferred.
- Grid is deferred.

Sizing rule shared by all solvers (`mel_layout_item_preferred`): `fixed_*` >
`preferred_*` > `natural_*` (the child's current or natively-measured size,
supplied by the host).

## Dependencies

`core`, `string` (str8 class names).

## Build & verify

    ./nob build layout
    ./nob test layout-test
