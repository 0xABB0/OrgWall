# messagebox — spec

## Purpose
Synchronous modal native alert. Callable before subsystem init for startup-failure reporting.

## Surface
- `mel_msgbox_alert_opt(title, message, opt) -> Mel_Msgbox_Status` (+ `mel_msgbox_alert` macro): OK-only.
- `mel_msgbox_show_opt(opt) -> Mel_Msgbox_Result` (+ `mel_msgbox_show` macro): custom buttons, returns chosen id.
- `mel_msgbox_available() -> bool`.

## Model
- Severity: `Mel_Msgbox_Severity` u32 value (info/warn/error). No enum.
- Status: `Mel_Msgbox_Status` u32, bits 0..1 severity (OK/WARNED/ERROR), flags `RESULT_*`/`WARN_*`, inline predicates.
- Button: descriptor `{ str8 label; i32 id; }`. Array owned by caller for the call's duration.
- Defaults: `default_id` = return key target, `escape_id` = escape/cancel target. Fall back to first/last button; no silent default beyond that (MEL-CODE-007: gated by `has_*`).
- Color: `{ bool has_value; mel_color8 value; }` for accent/text/background. No silent default.
- `right_to_left` reorders buttons. `parent` is a `Mel_Window`; lowered to a native handle via `mel_window_native`.

## Backend contract (`backend.h`)
One TU per platform implements:
- `bool mel_msgbox__plat_available(void)`
- `Mel_Msgbox_Status mel_msgbox__plat_show(const Mel_Msgbox_Request*, i32* out_chosen_id)`
The core lowers `Mel_Msgbox_Opt` into `Mel_Msgbox_Request` (defaults resolved, parent → native handle), so backends never read the public Opt.

## Honest degrade (MEL-ENGINE-VII)
- Capability the platform cannot offer is dropped and reported as a `WARN_*` bit, never silently.
- A platform with no dialog at all returns `ERROR | RESULT_NO_BACKEND` and picks `escape_id`.
- wasm/android collapse to fewer buttons with `WARN_BUTTONS_COLLAPSED`.

## Invariants
- No global state; no init; no allocator required on the common path.
- `button_count == 0` presents an implicit single OK button.
- Failure is loud (logged + ERROR status); never silent corruption (MEL-ENGINE-VIII).
