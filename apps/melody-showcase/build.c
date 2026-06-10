#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* app = mel_add_executable(b, "melody-showcase");
    mel_subsystem(app, "gui");

    mel_sources(app, ALWAYS, "src/*.c");

    mel_depends(app, "core");
    mel_depends(app, "string");
    mel_depends(app, "allocator");
    mel_depends(app, "log");
    mel_depends(app, "color");

    mel_depends(app, "vat");
    mel_depends(app, "event");
    mel_depends(app, "future");
    mel_depends(app, "executor");

    mel_depends(app, "boot");
    mel_depends(app, "gui");
    mel_depends(app, "window");
    mel_depends(app, "paint");
    mel_depends(app, "display");

    mel_depends(app, "platform");
    mel_depends(app, "cpu");
    mel_depends(app, "power");
    mel_depends(app, "time");
    mel_depends(app, "locale");
    mel_depends(app, "debug");

    mel_depends(app, "input");
    mel_depends(app, "gamepad");
    mel_depends(app, "sensor");
    mel_depends(app, "hid");

    mel_depends(app, "fs");
    mel_depends(app, "io");
    mel_depends(app, "storage");
    mel_depends(app, "process");
    mel_depends(app, "dylib");

    mel_depends(app, "dialog");
    mel_depends(app, "messagebox");
    mel_depends(app, "tray");
    mel_depends(app, "shell");
    mel_depends(app, "clipboard");
    mel_depends(app, "vibration");

    mel_manifest(app, "APP_LABEL", "Melody Showcase");
    mel_manifest(app, "BUNDLE_ID", "orgwall.melodyshowcase");
}
