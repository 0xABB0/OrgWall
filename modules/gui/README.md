# Melody GUI

Win32-shaped cross-platform UI layer.

The public surface is intentionally handle/message based:

```c
Mel_Gui_Handle window = mel_gui_create_window(S8("Melody"), 480, 320, app_proc, user);
Mel_Gui_Handle button = mel_gui_create_child(window, MEL_GUI_CLASS_BUTTON, S8("Hello"), 100, 24, 24, 160, 44);

while (mel_gui_get_message(&msg)) {
    mel_gui_dispatch_message(&msg);
}
```

## Shape

- `Mel_Gui_Handle` is the portable equivalent of `HWND`.
- `Mel_Gui_Proc` is the portable equivalent of `WNDPROC`.
- `Mel_Gui_Msg`, `Mel_Gui_WParam`, and `Mel_Gui_LParam` model message dispatch without exposing any one OS.
- Built-in class names such as `mel.window`, `mel.button`, and `mel.edit` map to native widgets where the platform has them.
- Backends decide what a handle means: `HWND` on Win32, `NSView*`/`NSWindow*` on macOS, `jobject` global refs on Android, DOM nodes on web, or rendered panels in VR.

## Rule

The core API stays OS-shaped. Convenience layers can be added above it, but the substrate should not grow into an object/vtable widget hierarchy.
