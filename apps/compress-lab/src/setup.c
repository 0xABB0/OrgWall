#include <boot/boot.h>
#include <vat/vat.h>

#include <allocator/heap.h>
#include <compress/brotli.h>
#include <compress/compress.h>
#include <compress/deflate.h>
#include <compress/lz4.h>
#include <compress/rle.h>
#include <compress/zstd.h>
#include <dialog/dialog.h>
#include <gui/gui.h>

#include <string.h>

void lab_ui_setup(Mel_Vat* root);
bool lab_smoke_run(void);

static bool smoke_requested(void)
{
    for (int i = 1; i < mel_app_argc(); i++)
        if (strcmp(mel_app_argv()[i], "--smoke") == 0)
            return true;
    return false;
}

void mel_app_setup(Mel_Vat* root)
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    mel_compress_registry_init(alloc);
    mel_compress_register(mel_compress_rle());
    mel_compress_register(mel_compress_gzip());
    mel_compress_register(mel_compress_lz4());
    mel_compress_register(mel_compress_zstd());
    mel_compress_register(mel_compress_brotli());
    mel_compress_register(mel_compress_deflate());

    if (smoke_requested())
    {
        mel_app_set_exit_code(lab_smoke_run() ? 0 : 1);
        return;
    }

    mel_gui_init(root);
    mel_dialog_init(alloc, root);

    lab_ui_setup(root);
}
