# Canvas Primitives — Software 2D and GPU Viewport

## Problem Statement

The widget model (PRD 06) requires a fallback primitive for widgets that have no native counterpart on a given backend: a surface they can draw into and receive pointer events from. The model also needs to support games and GPU-rendered content, which are a different category — those want raw access to a swapchain backed by Metal / Vulkan / D3D12 / WebGPU, not a 2D drawing API.

The two needs differ enough that fusing them into one primitive bloats the API surface and forces every consumer to ignore half the functions. A widget drawing a knob needs filled rectangles, paths, and text; it does not need vertex buffers and pipelines. A game viewport needs swapchain images and command queues; it does not need stroked text.

There is also the cross-backend consistency question: if every backend exposes its native 2D API directly, custom widgets become per-backend-specific even when they want to be portable. A single neutral drawing vocabulary, implemented per-backend on top of that backend's native 2D API, lets a canvas widget be written once and run anywhere.

## Solution

Two distinct primitives, kept clearly separate:

- **`Mel_Gui_Canvas`** — a 2D drawing surface. Provides a neutral drawing API of approximately 20 functions (filled rect, stroked rect, filled path, stroked path, draw text, blit image, transform stack push/pop/translate/scale/rotate, clip push/pop). Implemented per-backend on top of `CGContextRef` (Cocoa), `android.graphics.Canvas` (Android Views), GDI `HDC` or Direct2D (Win32), Canvas 2D context (web DOM), or equivalent. The drawing vocabulary does not vary by backend.

- **`Mel_Gui_Viewport`** — a GPU-bound surface. Provides access to a backend-appropriate swapchain: `CAMetalLayer` (Cocoa, iOS, visionOS), `VkSurfaceKHR` (Linux/Android), `IDXGISwapChain`-bound HWND (Win32), WebGPU context (web). Consumer drives its own rendering using its chosen GPU API. The framework provides the surface and the lifecycle messages around it; it does not provide a drawing API.

Custom widgets that need to paint themselves use `Mel_Gui_Canvas`. Game viewports and any GPU-rendered content use `Mel_Gui_Viewport`. Mixed apps use both as siblings in the widget tree (a level editor with a docked GPU preview plus surrounding native UI; a form with a small canvas-drawn visualization).

## Implementation Decisions

`Mel_Gui_Canvas` exposes the following drawing vocabulary, fixed and neutral:

- Fill operations: filled rectangle, filled rounded rectangle, filled circle, filled ellipse, filled path
- Stroke operations: stroked rectangle, stroked path, stroked line, stroked polyline (with configurable width, join, cap)
- Text: draw text with a font handle, measure text
- Image: blit image at position, blit image scaled to rect
- Transforms: push, pop, identity, translate, scale, rotate, multiply
- Clipping: push rect clip, push path clip, pop clip
- Colors: solid color, linear gradient, radial gradient (where supported; backends without gradient support degrade to nearest solid)

The exact function count and signatures are an implementation detail of the canvas module; ~20 functions is the target. Each function maps to a small number of calls in each backend's native 2D API.

Each canvas instance owns a paint callback and a pointer callback installed at creation time. The paint callback receives the canvas handle and a pointer to the canvas instance state; the pointer callback receives pointer events that hit the canvas's bounds.

Backends without a usable 2D API (rare; possibly some XR backends) do not get a canvas implementation and can only host widgets that have native implementations or no widgets at all.

`Mel_Gui_Viewport` exposes its surface via callbacks at creation time and via lifecycle events:

- `on_surface_created(handle, Mel_Gpu_Surface*, user)` fires when the platform has finished setting up the surface
- `on_surface_resized(handle, w, h, user)` fires on size changes
- `on_surface_destroyed(handle, user)` fires when the surface goes away
- `on_frame(handle, dt_ns, user)` fires on each vsync if frames are opted in

`Mel_Gpu_Surface` carries a tagged union: backend kind (Metal / Vulkan / D3D12 / WebGPU / null) and the platform-specific handles needed to construct a swapchain on that backend.

A viewport's input callbacks (pointer, keyboard) work like any other widget's via the capability callback structs (PRD 05). Input is delivered to the viewport directly; it is not translated to a 2D coordinate system the canvas exposes.

Canvas and viewport are both regular `Mel_Gui_Handle`s and can be children of any other handle (stage, panel, custom widget) and can have children themselves. A viewport with a stage as parent embeds the rendered surface in a UI; a viewport as a stage's only child is a full-screen game.

## Testing Decisions

A good test of the canvas verifies that calling each drawing function produces the expected result in a captured backing image — fill a red rectangle, read pixels, assert red in the expected region. The exact pixel values depend on backend rendering precision and antialiasing, so tests use tolerance bounds, not exact equality.

A good test of the viewport verifies that surface creation produces a valid surface struct of the expected backend kind, that resize callbacks fire with new dimensions on platform-driven resizes, that frame callbacks fire at approximately the platform's vsync rate when frames are opted in.

Modules under test: the canvas module (per-backend implementations + the public API), the viewport module (per-backend implementations + lifecycle).

Prior art: `mel_old/gpu.*` had Vulkan-shaped tests for surface acquisition. The pattern (create surface, assert valid handle, destroy, assert clean teardown) is reusable.

## Out of Scope

- A retained-mode 2D scene graph layered on top of the canvas. The canvas is immediate-mode-only: each `on_paint` redraws the visible state.
- A unified GPU abstraction layer (mel_gpu) that hides Metal/Vulkan/D3D12 differences. The viewport hands you a platform-native surface; consumers use Vulkan / Metal / D3D / WebGPU directly. A higher-level GPU layer is a future PRD.
- Vector graphics import (SVG, PDF). Out of canvas scope; if added later, lives in a separate module.
- Font selection, system font enumeration. The canvas accepts a font handle; the font system is a separate module.
- Software rendering on backends that have no 2D API (e.g., hypothetical pure-OpenXR-compositor backend). If a backend cannot host a canvas, widgets without native impls cannot run on it.

## Further Notes

The canvas drawing API is neutral, not "Cocoa-shaped" or "Android-shaped." Each backend translates the neutral calls into its own primitives. Where a backend's native API has no equivalent for a neutral operation, the backend implements it from lower-level primitives (e.g., gradient via multiple solid-color band fills, if necessary). The neutral API is fixed; backends adapt.

The viewport's `Mel_Gpu_Surface` is the boundary between the framework and consumer GPU code. The framework's responsibility ends at handing over the surface; consumers (or a future GPU abstraction module) construct swapchains, command buffers, pipelines on top of it.

Frame timing for viewports is handled per PRD 11 (Frame Timing). Canvas widgets that animate also use the same opt-in mechanism: `mel_gui_request_frames(canvas_handle, true)` makes the canvas receive `on_frame` callbacks alongside `on_paint`.
