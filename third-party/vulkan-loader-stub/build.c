#include "build.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool mel_vkstub__file_exists(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static bool mel_vkstub__generate(void)
{
    const char* headers = "third-party/vulkan-headers/include/vulkan";
    const char* outdir = "third-party/vulkan-loader-stub/build";
    const char* sofile = "third-party/vulkan-loader-stub/build/libvulkan.so";

    if (mel_vkstub__file_exists(sofile))
        return true;
    if (!mel_vkstub__file_exists("third-party/vulkan-headers/include/vulkan/vulkan_core.h"))
        return false;

    char cmd[4096];
    snprintf(cmd, sizeof cmd,
             "mkdir -p '%s' && "
             "{ grep -hoE 'VKAPI_CALL +vk[A-Za-z0-9]+' "
             "'%s/vulkan_core.h' '%s/vulkan_wayland.h' '%s/vulkan_xcb.h' "
             "| sed -E 's/.*VKAPI_CALL +//' | sort -u "
             "| while read s; do printf 'void %%s(void){}\\n' \"$s\"; done; } > '%s/_vkstub.c' && "
             "zig cc -target x86_64-linux-gnu -shared -fPIC -nostdlib "
             "-Wl,-soname,libvulkan.so.1 -o '%s' '%s/_vkstub.c'",
             outdir, headers, headers, headers, outdir, sofile, outdir);

    if (system(cmd) != 0)
        return false;
    return mel_vkstub__file_exists(sofile);
}

void build(Mel_Build* b)
{
    Mel_Target* tp = mel_add_third_party(b, "vulkan-loader-stub");

    if (!mel_vkstub__generate())
        return;

    char* dir = realpath("third-party/vulkan-loader-stub/build", NULL);
    if (!dir)
        return;
    char* flag = malloc(strlen(dir) + 4);
    snprintf(flag, strlen(dir) + 4, "-L%s", dir);
    mel_link(tp, MEL_PUBLIC, WHEN(.gpu = "vulkan", .platforms = MEL_ON(LINUX)), flag);
}
