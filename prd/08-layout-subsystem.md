# Layout Subsystem

## Problem Statement

Every existing application in the repo lays out its UI in absolute pixel coordinates. This does not survive DPI variance (Retina vs. non-Retina, Android dp scaling vs. raw Win32 pixels), does not survive window resize, does not survive cross-platform display differences, and is unmaintainable beyond a small number of children. nana's `place` DSL is the cleanest example I have seen of a layout system that scales without bloat. I want something equivalent — but not mandatory.

For trivial UIs (a viewport that fills the stage; a panel with three children at fixed positions; a custom widget that draws itself into known dimensions), forcing a layout system is overhead. For non-trivial UIs (most form-shaped apps), forcing manual coordinates is unmaintainable. The library should support both.

I also want flexibility about *which* layout strategy applies. Flex (CSS-flexbox style), constraint-based (Cassowary), grid (CSS-grid), and string-DSL (nana's `place`) all have valid use cases. The library should not lock me into one.

## Solution

Layout lives in a separate optional module `gui.layout`. Apps that do not include it lay out children with the `x/y/w/h` fields in widget creation options (the current model). Apps that include it can attach a layout to any handle with children, and the layout computes positions and sizes for those children whenever the parent resizes or a child's layout properties change.

Multiple layout strategies are supported, each in its own sibling sub-module: `gui.layout.flex`, `gui.layout.constraint`, `gui.layout.grid`, `gui.layout.dsl` (string-based, nana-style). An application chooses which strategies to link based on which it uses; unused strategies cost nothing.

Each layout strategy implements a small uniform interface: given a parent's content rectangle and a list of children with their layout metadata, compute each child's rectangle. The framework handles the rest: triggering re-layout on parent resize, propagating size constraints up the tree, calling each child's `on_resize` callback when its rectangle changes.

Composition works without layout: if you create a panel with three children and no layout, the children retain whatever `x/y/w/h` they were created with. Adding a layout later (or at creation time) makes the framework manage their geometry from then on.

## Implementation Decisions

- `gui.layout` is the parent module. Sub-modules `gui.layout.flex`, `gui.layout.constraint`, `gui.layout.grid`, `gui.layout.dsl` ship as separate modules; each can be linked or not.
- Attaching a layout to a handle: `mel_gui_set_layout(handle, layout_handle)`. The layout handle is created by the strategy module's own create function (`mel_gui_layout_flex_create_opt`, etc.). Each layout strategy has its own creation options matching its model.
- Child layout properties (flex grow/shrink/basis, grid row/column spans, constraint anchors, DSL field names) are set on the child handle via strategy-specific setters. The same widget can carry layout properties for multiple strategies; only the parent's selected strategy reads the relevant ones.
- A handle without a layout retains its `x/y/w/h` from creation and is not affected by sibling changes.
- A handle whose parent has a layout has its `x/y/w/h` managed by the layout. Direct calls to `mel_gui_set_window_pos` on a layout-managed child are valid but the next layout pass will overwrite them.
- The framework triggers layout recomputation when the parent's content rectangle changes (window resize, parent layout-managed resize) or when a child's relevant layout properties change.
- Layout strategies are not allowed to depend on each other. Each is a self-contained module with one interface to the framework.
- The DSL strategy parses its string at layout-create time, not on every recompute. The parsed form is cached on the layout handle.

## Testing Decisions

A good test of a layout strategy verifies that given a known parent size and known children with known layout properties, the strategy produces the expected child rectangles. Tests are deterministic and do not require rendering — only computed rectangles are compared.

Modules under test: each strategy sub-module independently, plus the framework's layout integration (triggering re-layout on resize).

Prior art: nana's place DSL has well-documented test cases for typical layouts that can inform a test suite for the DSL strategy. Flex has the CSS-flexbox specification's reference test cases.

## Out of Scope

- Animation of layout transitions. If a child's rectangle changes from frame to frame due to a layout recompute, the transition is instant. Animated layout interpolation is future work, possibly a separate module.
- Layout debugging visualization (highlighting layout boundaries, showing flex regions). Useful, but not load-bearing.
- Bidirectional / RTL layout support. Strategies may handle it; not a framework concern.
- Cross-strategy conversion (importing a CSS flex spec into the DSL, for instance). Each strategy stands on its own.
- Determining which strategy is "best" for a given use case. Apps choose.

## Further Notes

The framework does not require a default layout strategy. Recommending one in docs (likely flex, as the most broadly familiar) is a documentation concern, not a framework concern.

Layout integrates with the widget composition model (PRD 06): a widget whose visual is composition typically creates child widgets and applies a layout to organize them. The layout is part of the widget's behavior, not the framework's responsibility, and is attached by the widget's create function.

For widgets that need different layouts on different backends (rare but possible — e.g., a settings panel that uses native Android Preferences layout on android.views and a flex layout on Win32), the per-backend implementations choose their own layout strategies independently.
