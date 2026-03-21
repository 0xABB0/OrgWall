# render.manager

## TODO

- Spaces are still low-level typed payload blobs with no higher-level scene helpers. Add explicit scene-facing helpers for common space kinds so sources do not keep reinventing local payload conventions.
- The manager now owns instance/material/space truth, but there is still no first-class scene-global model for lights, environment, or other world-level render state. Add that without contaminating technique caches.
- Material bindings are stored per instance, but there is no richer binding model yet for things like named slots, submesh conventions, or validation of source-emitted binding indices. Tighten that contract so bad bindings fail early and clearly.
