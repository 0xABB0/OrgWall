# Melody GUI

The GUI stack is split by responsibility. The core module owns handles, class registration, messages, dispatch, and platform realization hooks. Controls, app lifecycle vocabulary, input vocabulary, layout primitives, accessibility semantics, and platform bindings live in separate modules.

## Modules

- `gui`: core handles, class registration, create/destroy, message send/dispatch, and direct platform hook declarations.
- `gui.control`: built-in control contracts. Each built-in class has its own header: `window.h`, `panel.h`, `label.h`, `button.h`, `edit.h`, `checkbox.h`, `slider.h`.
- `gui.layout`: shared coordinate/unit/rect vocabulary. It does not implement a layout engine yet.
- `gui.input`: platform-neutral input event vocabulary for keyboard, pointer, touch, pen, gaze, and controllers.
- `gui.accessibility`: semantic roles, states, names, values, and hints.
- `gui.app`: app/surface lifecycle vocabulary for activity/window/page/session style hosts.
- `gui.platform.android`: Android JNI realization of core controls into native Android views.

There is intentionally no `gui.render` module yet. Native Android controls are realized by the Android platform module; custom rendering is a separate design problem.

## Shape

```c
#include <gui.control/gui.control.h>
#include <gui.platform.android/gui.platform.android.h>

Mel_Gui_Handle root = mel_gui_create_window(S8("Melody"), 480, 320, root_proc, user);
Mel_Gui_Handle button = mel_gui_create_button(root, S8("Hello"), 100, 24, 24, 160, 44);

mel_gui_send_message(button, MEL_GUI_MSG_CLICK, 0, 0);
```

Custom classes are registered in core, and the platform module only receives the class it should realize. This keeps the Win32-like message/class model independent from Android, Web, Cocoa, VR, or a custom OS surface.

```c
mel_gui_register_class(&(Mel_Gui_Class_Desc){
    .name = S8("demo.counter_button"),
    .base_name = MEL_GUI_CLASS_BUTTON,
    .proc = counter_button_proc,
});

Mel_Gui_Handle button = mel_gui_create_child(
    root, S8("demo.counter_button"), S8("Tap"), 42, 24, 24, 160, 44);
```

## Platform Hooks

`gui` calls direct platform functions declared by `gui.platform.h`, such as `mel_gui_platform_realize`, `mel_gui_platform_set_text`, and `mel_gui_platform_set_rect`. A platform module implements those symbols directly. This is not a backend vtable; the platform is selected by the objects linked for the target.
