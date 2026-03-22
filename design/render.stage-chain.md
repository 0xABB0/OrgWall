# Melody Render Stage Chain

This note defines the forward-facing ownership split and hook model for Melody's render chain.

It exists because a fixed scene-owned `environment` struct was the wrong answer.

The engine needs a design that:

- stays open
- does not hardcode one lighting model
- lets the app hook in at deliberate points
- keeps scene truth, view truth, and execution truth separate

## Goals

- scene truth stays canonical and minimal
- view truth owns observation and output response
- techniques derive execution data
- app code can hook into explicit stages
- no fixed lighting approximation is treated as canonical truth

## Ownership split

### 1. Scene-owned truth

Scene-owned render truth includes:

- instances
- source bindings
- material bindings
- direct light emitters
- shadow intent
- future environment/world inputs:
  - world source reference
  - probe collections
  - atmosphere / media inputs
  - other authored lighting contributors

Scene-owned truth does **not** include:

- exposure
- tonemap
- output response policy
- renderer-specific environment approximations

### 2. View-owned truth

View-owned truth includes:

- camera
- target
- visibility mask
- output intent
- response policy

Response policy includes things like:

- exposure
- tonemap
- output conversion
- debug response overrides

These are properties of observing/rendering the scene, not properties of the scene itself.

### 3. Technique-owned truth

Technique-owned truth is derived execution data:

- visibility structures
- shadow render data
- light-selection data
- draw generation / submission data
- transparency queues
- resolve data

This belongs to the technique cache or stage data, never the canonical scene.

## Stage chain

For a scene-rendering technique like `scene_forward`, the chain should be explicit.

Minimum stage set:

1. scene input gather
2. visibility
3. shadow preparation
4. shadow rendering
5. lighting input preparation
6. main shading
7. transparency
8. output response / resolve

These are conceptual stages.
They do not require one file per stage, but they do require explicit ownership and hook seams.

## Hook seams

The app or higher-level engine code should be able to hook into specific stages.

Minimum hook categories:

### Before visibility

Use cases:

- custom culling
- debug masking
- special portal visibility
- software LOD selection

### Before shadow preparation

Use cases:

- custom shadow caster filtering
- alternate shadow setup
- special shadow policies

### Before lighting input preparation

Use cases:

- inject custom world/environment input
- probe selection
- atmosphere contribution
- game-specific lighting contributors

### Before main shading

Use cases:

- custom material-domain data
- alternate light selection
- technique-specific shading extensions

### Before transparency

Use cases:

- special transparency submission
- alternate sort policy
- custom order-independent transparency path

### Before output response / resolve

Use cases:

- exposure
- tonemap
- debug view transforms
- output conversion
- special compositing

## What this means for current Melody code

### `render.scene`

Keep:

- ambient for now
- directional lights
- point lights
- shadow intent

Do not add:

- exposure
- tonemap
- fixed `sky/ground` response approximation

Future richer environment support should enter as authored world/environment inputs, not as renderer-convenience constants.

### `render.view`

This is where response policy should eventually live.

When exposure arrives, it should land here or in a view-owned response object, not in `render.scene`.

### `scene_forward`

`scene_forward` may derive:

- ambient approximation
- hemisphere approximation
- sky lookup
- clustered lights
- shadow strategy

But those are technique execution decisions, not scene truth.

## Immediate implementation consequences

Before more lighting work lands:

1. keep `M3` partial
2. keep direct lights in `render.scene`
3. do not add exposure or fixed environment approximation to scene truth
4. design the view/output response interface
5. design the first explicit stage hook seam

## First concrete next interfaces to negotiate

1. view-owned response contract
2. scene-owned world/environment input contract
3. stage hook registration contract for `scene_forward`

Only after those are defined should richer environment lighting continue.
