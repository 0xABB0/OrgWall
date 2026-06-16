# Modern Win32 UI (C++)

A small desktop app built with **only the Windows API** — no UI frameworks —
that demonstrates a modern Windows 11 look using native common controls plus a
pair of custom toggle switches.

![screenshot](screenshot.png)

## What makes it "modern"

| Feature | API used |
| --- | --- |
| Immersive **dark title bar** | `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)` |
| Windows 11 **rounded corners** | `DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE)` |
| **Mica / Acrylic** backdrop (toggle) | `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE)` |
| **Dark themed** native controls | `SetWindowTheme(hwnd, L"DarkMode_Explorer")` + undocumented `SetPreferredAppMode` |
| Crisp **Segoe UI Variable** text | `CreateFontW(... L"Segoe UI Variable Text")` |
| **Per-Monitor-V2 DPI** awareness | `SetProcessDpiAwarenessContext` + `WM_DPICHANGED` |
| Native controls (v6) | manifest dependency on `Microsoft.Windows.Common-Controls` |
| Animated **toggle switches** | owner-drawn control rendered with **GDI+** |

Most of the UI is standard controls — list box (nav), edit, combo box,
trackbar, progress bar, push buttons — so there is no hand-painted chrome.

## The toggle switch

A sliding toggle is **not** a native Win32 control (Windows only ships it in
WinUI/XAML), so the only way to get one in pure Win32 is to draw it. It lives
in its own self-contained window class, `ModernToggle`:

- Registered with `RegisterClassExW`, used like any other control (it sends
  `WM_COMMAND` / `BN_CLICKED` to the parent, supports `WS_TABSTOP` + Space).
- Rendered with **GDI+** (`Gdiplus::Graphics`) so the pill and knob are
  anti-aliased.
- Animates the knob between states on a `WM_TIMER` and grows it slightly on
  hover; double-buffered to avoid flicker.

Three of them are wired up: **Enable notifications**, **Dark mode** (re-themes
the whole window live), and **Acrylic backdrop** (switches the DWM system
backdrop between Mica and Acrylic at runtime).

## Build

The project builds with **Bazel** (MSVC toolchain on Windows):

```bat
bazel build //:ModernUI
bazel-bin\ModernUI.exe
```

`app.manifest` (common-controls v6 + Per-Monitor-V2 DPI) is embedded by the
linker via `/MANIFEST:EMBED`.

## IDE / clangd setup (compile_commands.json)

The project wires up the [Hedron Compile Commands Extractor][hedron] so clangd,
clang-tidy, and other libclang tooling get the exact MSVC flags Bazel uses:

```bat
bazel run //:refresh_compile_commands
```

This writes `compile_commands.json` to the workspace root (re-run it whenever
sources, flags, or dependencies change).

Because this repo targets **Bazel 9**, where the native `py_*` and `cc_*` rules
were removed, `.bazelrc` enables the autoload bridge
(`--incompatible_autoload_externally=...`) so the extractor's
`native.py_binary` / `cc_binary` calls resolve against `@rules_python` /
`@rules_cc`.

[hedron]: https://github.com/hedronvision/bazel-compile-commands-extractor

## Notes

- Dark-mode controls rely on the undocumented `uxtheme.dll` ordinal #135
  (`SetPreferredAppMode`) — the same mechanism Explorer uses. It is resolved at
  runtime and degrades gracefully on older builds.
- Mica / Acrylic / rounded corners require **Windows 11**; on Windows 10 the
  DWM calls are no-ops and the window falls back to a solid dark background.
