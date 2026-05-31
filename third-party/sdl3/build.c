#include "build.h"

void build(Mel_Build *b) {
    Mel_Target *t = mel_add_third_party(b, "sdl3");
    mel_unavailable(t, WHEN(.platforms = MEL_ON(WASM)));
    mel_cmake(t, "SDL", "-DSDL_SHARED=OFF", "-DSDL_STATIC=ON", "-DSDL_TEST_LIBRARY=OFF", "-DSDL_TESTS=OFF",
              "-DSDL_EXAMPLES=OFF", "-DSDL_INSTALL_TESTS=OFF", "-DSDL_DISABLE_INSTALL_DOCS=ON",
              "-DSDL_AUDIO=OFF", "-DSDL_VIDEO=OFF", "-DSDL_GPU=OFF", "-DSDL_RENDER=OFF", "-DSDL_CAMERA=OFF",
              "-DSDL_JOYSTICK=OFF", "-DSDL_HAPTIC=OFF", "-DSDL_HIDAPI=OFF", "-DSDL_POWER=OFF",
              "-DSDL_SENSOR=OFF", "-DSDL_DIALOG=OFF", "-DSDL_OPENGL=OFF", "-DSDL_OPENGLES=OFF",
              "-DSDL_VULKAN=OFF");
    mel_link(t, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "-lSDL3-static");
    mel_link(t, MEL_PUBLIC,
             WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS) | MEL_ON(LINUX) | MEL_ON(ANDROID)), "-lSDL3");
}
