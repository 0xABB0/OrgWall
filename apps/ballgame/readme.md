# ballgame

A pure game on the framework: one ball, moved with WASD (drag steers it on touch
platforms). Exercises the full interactive loop — boot entry, vat tick at 60 Hz,
GUI canvas, paint, keyboard and pointer input — on every platform the framework
boots on (macos, win32, ios, android, wasm).

Deps: boot, vat, allocator, core, gui, paint, color, math, string, time.
