# render.response todo

- add more built-in response ops only when their ownership is clearly view/output-side
- add a dedicated auto-exposure op with:
  - view-authored params
  - technique-owned temporal state
- decide whether response-op execution should eventually become reusable outside `scene_forward`
- pressure-test the current `user_data` contract with a real app-driven animated response path
- add focused tests for:
  - param alignment
  - ordering
  - handle lifetime
  - cleanup on view destroy
- decide whether response ops should gain explicit debug naming in runtime diagnostics
