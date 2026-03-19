# render.source.composite

## TODO

- Composite sources currently reuse the first child's manager implementation and then overwrite every child's `manager` pointer before sync. This is brittle and unsafe for mixed source types because it assumes all children agree on manager layout, pool schema, and lifecycle. Redesign composition around an explicit shared manager contract or around multiple managers instead of pointer patching.
