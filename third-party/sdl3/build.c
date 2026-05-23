#include "build.h"

static const char *const k_sdl_args[] = {
    "-DSDL_SHARED=OFF", "-DSDL_STATIC=ON",
    "-DSDL_TEST_LIBRARY=OFF", "-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF",
    "-DSDL_INSTALL_TESTS=OFF", "-DSDL_DISABLE_INSTALL_DOCS=ON",
    "-DSDL_AUDIO=OFF", "-DSDL_VIDEO=OFF", "-DSDL_GPU=OFF", "-DSDL_RENDER=OFF",
    "-DSDL_CAMERA=OFF", "-DSDL_JOYSTICK=OFF", "-DSDL_HAPTIC=OFF",
    "-DSDL_HIDAPI=OFF", "-DSDL_POWER=OFF", "-DSDL_SENSOR=OFF", "-DSDL_DIALOG=OFF",
    "-DSDL_OPENGL=OFF", "-DSDL_OPENGLES=OFF", "-DSDL_VULKAN=OFF",
};

static bool build_sdl3(Mel_Build_Context *ctx) {
    return mel_tp_cmake(ctx, "third-party/sdl3", k_sdl_args,
                        sizeof(k_sdl_args) / sizeof(k_sdl_args[0]), "libSDL3.a");
}

bool project(Mel_Build_Target *t) {
    mel_build_set_name(t, "sdl3");
    mel_build_set_kind(t, MEL_TARGET_THIRD_PARTY);
    mel_build_add_link_flag(t, MEL_PUBLIC, "-lSDL3");
    mel_build_suppress_default(t, MEL_STAGE_COMPILE);
    mel_build_suppress_default(t, MEL_STAGE_LINK);
    mel_build_on_compile(t, build_sdl3);
    return true;
}
