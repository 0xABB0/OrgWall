# render.response todo

- add more built-in response ops only when their ownership is clearly view/output-side
- add a dedicated auto-exposure op with:
  - view-authored params
  - technique-owned temporal state
- decide whether tonemap ops should eventually accept explicit authored params or remain fixed named operators (`aces`, `reinhard`, etc.)
- decide whether response-op execution should eventually become reusable outside `scene_forward`
- pressure-test the current `user_data` contract with a real app-driven animated response path
- add focused tests for:
  - param alignment
- decide whether response ops should gain explicit debug naming in runtime diagnostics
- the resolve contract needed both `dst_target` and optional `dst` image. Keep that split explicit. The final step may land on a swapchain target, so a raw destination image pointer is not a sufficient public contract.
- resolve passes must own both sides of their image-layout story:
  - source transition to shader-read
  - offscreen destination transition to color-attachment
  Do not rely on whatever layout a previous pass happened to leave behind.
- fullscreen response passes use a different Y convention than the main scene pass. The main renderer currently uses flipped Vulkan viewports in several places; resolve/post passes must make their own convention explicit instead of inheriting that flip blindly.
- public helper macros need dedicated parameter names. The first `mel_render_view_response_op_add(...)` macro used field names like `view` and `type` as macro parameters and broke expansion in a non-obvious way.
