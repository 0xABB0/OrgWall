# render.response

`render.response` owns the per-view response / resolve chain.

It exists to keep output response truth on the view side instead of smuggling it into:

- `render.scene`
- `scene_forward`
- material state

## What it owns

- per-view ordered response op lists
- copied response-op params
- built-in response op implementations

## What it does not own

- scene lighting truth
- environment truth
- visibility
- shadows
- generic stage hooks
- response ping-pong targets

Those remain technique-owned execution data.

## Current contract

Each response op:

1. reads a source image
2. receives a ready-to-sample source binding index
3. renders into a destination target

The current public seam is intentionally narrow:

- add/remove/clear per-view response ops
- execute them during `scene_forward` resolve

## Current built-in op

- `mel_render_response_exposure_manual`
- `mel_render_response_tonemap_aces`

This is deliberately small.
Auto-exposure is not faked here.

## Important ownership rules

- params are copied at registration time
- `user_data` is not copied
- `user_data` must remain valid while the op is registered
- param storage honors the declared alignment

## Cost rules

- if a view has no response ops, `scene_forward` must elide the resolve chain
- ping-pong intermediates are created only when the chain length requires them

This is important for the engine rule that unused work should not be paid for.
