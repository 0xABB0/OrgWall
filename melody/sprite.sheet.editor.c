#include "sprite.sheet.editor.h"

void mel_ed_spritesheet_init(Mel_EdSpritesheet* ed, const Mel_Alloc* alloc)
{
    *ed = (Mel_EdSpritesheet){0};
    ed->alloc = alloc;
}

void mel_ed_spritesheet_shutdown(Mel_EdSpritesheet* ed)
{
    (void)ed;
}

void mel_ed_spritesheet_draw(Mel_EdSpritesheet* ed, f32 dt)
{
    (void)ed;
    (void)dt;
}
