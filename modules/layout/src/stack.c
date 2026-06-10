#include <layout/stack.h>

static void stack_measure(const Mel_Layout* layout, const Mel_Layout_Item* items, u32 count, i32 avail_w, i32 avail_h, i32* out_w, i32* out_h)
{
    const Mel_Stack_Layout* st = (const Mel_Stack_Layout*)layout;
    (void)avail_w;
    (void)avail_h;

    i32 w = 0;
    i32 h = 0;

    for (u32 i = 0; i < count; i++)
    {
        i32 pw, ph;
        mel_layout_item_preferred(&items[i], &pw, &ph);
        pw += items[i].spec.margin_l + items[i].spec.margin_r;
        ph += items[i].spec.margin_t + items[i].spec.margin_b;
        if (pw > w)
            w = pw;
        if (ph > h)
            h = ph;
    }

    if (out_w)
        *out_w = w + st->margin * 2;
    if (out_h)
        *out_h = h + st->margin * 2;
}

static void stack_arrange(const Mel_Layout* layout, Mel_Layout_Item* items, u32 count, i32 avail_w, i32 avail_h)
{
    const Mel_Stack_Layout* st = (const Mel_Stack_Layout*)layout;

    for (u32 i = 0; i < count; i++)
    {
        Mel_Layout_Item* it = &items[i];
        it->x = st->margin + it->spec.margin_l;
        it->y = st->margin + it->spec.margin_t;
        it->w = avail_w - st->margin * 2 - it->spec.margin_l - it->spec.margin_r;
        it->h = avail_h - st->margin * 2 - it->spec.margin_t - it->spec.margin_b;
        if (it->w < 0)
            it->w = 0;
        if (it->h < 0)
            it->h = 0;
    }
}

static const Mel_Layout_Class s_stack_class = {
    .name = { .data = (u8*)"stack", .len = 5 },
    .measure = stack_measure,
    .arrange = stack_arrange,
};

const Mel_Layout_Class* mel_stack_layout_class(void) { return &s_stack_class; }

void mel_stack_layout_init(Mel_Stack_Layout* layout, Mel_Stack_Layout_Opt opt)
{
    layout->base.cls = &s_stack_class;
    layout->margin = opt.margin;
}
