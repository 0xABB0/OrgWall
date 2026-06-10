#include "showcase.h"

#include <string.h>

#include <boot/boot.h>

void mel_app_setup(Mel_Vat* root)
{
    int    argc = mel_app_argc();
    char** argv = mel_app_argv();
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--smoke") == 0)
        {
            showcase_smoke_setup(root);
            return;
        }
    }
    showcase_window_setup(root);
}
