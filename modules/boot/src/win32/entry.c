#include "../boot_internal.h"

#include <allocator/heap.h>
#include <boot/boot.h>
#include <vat/vat.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>

static int boot_run(int argc, char** argv)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    mel_boot__init(argc, argv, alloc);

    Mel_Vat_Waiter* waiter = mel_vat_waiter_ui(alloc);
    Mel_Vat_Driver* driver = mel_vat_driver_fair(alloc, 64);
    Mel_Vat*        root = mel_vat_open(alloc, (Mel_Vat_Desc){ .waiter = waiter, .driver = driver });

    mel_boot__lifecycle_init(root, alloc);
    mel_app_setup(root);
    mel_vat_run(root);

    int code = mel_boot__finish();
    mel_boot__lifecycle_shutdown();
    mel_vat_close(root);
    driver->vt->close(driver);
    waiter->vt->close(waiter);
    return code;
}

int main(int argc, char** argv) { return boot_run(argc, argv); }

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev, LPSTR cmdline, int show)
{
    (void)instance;
    (void)prev;
    (void)cmdline;
    (void)show;
    return boot_run(__argc, __argv);
}
