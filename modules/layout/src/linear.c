#include <layout/linear.h>

void mel_layout_item_preferred(const Mel_Layout_Item* item, i32* out_w, i32* out_h)
{
    const Mel_Layoutable* s = &item->spec;
    if (out_w)
        *out_w = s->fixed_w ? s->fixed_w : s->preferred_w ? s->preferred_w : item->natural_w;
    if (out_h)
        *out_h = s->fixed_h ? s->fixed_h : s->preferred_h ? s->preferred_h : item->natural_h;
}

static u8 resolve_cross(u8 child_align, u8 default_align)
{
    if (child_align == MEL_ALIGN_DEFAULT)
        return default_align == MEL_ALIGN_DEFAULT ? MEL_ALIGN_START : default_align;
    return child_align;
}

static i32 margin_main_lead(const Mel_Layoutable* s, bool vertical) { return vertical ? s->margin_t : s->margin_l; }
static i32 margin_main_trail(const Mel_Layoutable* s, bool vertical) { return vertical ? s->margin_b : s->margin_r; }
static i32 margin_cross_lead(const Mel_Layoutable* s, bool vertical) { return vertical ? s->margin_l : s->margin_t; }
static i32 margin_cross_trail(const Mel_Layoutable* s, bool vertical) { return vertical ? s->margin_r : s->margin_b; }

static void preferred_axes(const Mel_Layout_Item* item, bool vertical, i32* main_out, i32* cross_out)
{
    i32 pw, ph;
    mel_layout_item_preferred(item, &pw, &ph);
    *main_out = vertical ? ph : pw;
    *cross_out = vertical ? pw : ph;
}

static void linear_measure(const Mel_Layout* layout, const Mel_Layout_Item* items, u32 count, i32 avail_w, i32 avail_h, i32* out_w, i32* out_h)
{
    const Mel_Linear_Layout* lin = (const Mel_Linear_Layout*)layout;
    (void)avail_w;
    (void)avail_h;

    i32 primary = 0;
    i32 cross = 0;

    for (u32 i = 0; i < count; i++)
    {
        i32 pm, pc;
        preferred_axes(&items[i], lin->vertical, &pm, &pc);
        pm += margin_main_lead(&items[i].spec, lin->vertical) + margin_main_trail(&items[i].spec, lin->vertical);
        pc += margin_cross_lead(&items[i].spec, lin->vertical) + margin_cross_trail(&items[i].spec, lin->vertical);

        primary += pm;
        if (pc > cross)
            cross = pc;
    }

    if (count > 1)
        primary += lin->spacing * (i32)(count - 1);
    primary += lin->margin * 2;
    cross += lin->margin * 2;

    if (out_w)
        *out_w = lin->vertical ? cross : primary;
    if (out_h)
        *out_h = lin->vertical ? primary : cross;
}

static void linear_arrange(const Mel_Layout* layout, Mel_Layout_Item* items, u32 count, i32 avail_w, i32 avail_h)
{
    const Mel_Linear_Layout* lin = (const Mel_Linear_Layout*)layout;
    const bool               v = lin->vertical;

    i32 fixed_primary = 0;
    i32 total_weight = 0;

    for (u32 i = 0; i < count; i++)
    {
        i32 pm, pc;
        preferred_axes(&items[i], v, &pm, &pc);
        fixed_primary += pm + margin_main_lead(&items[i].spec, v) + margin_main_trail(&items[i].spec, v);
        total_weight += items[i].spec.weight;
    }

    if (count > 1)
        fixed_primary += lin->spacing * (i32)(count - 1);

    i32 container_main = (v ? avail_h : avail_w) - lin->margin * 2;
    i32 container_cross = (v ? avail_w : avail_h) - lin->margin * 2;
    i32 leftover = container_main - fixed_primary;
    if (leftover < 0)
        leftover = 0;

    i32 cursor = lin->margin;

    for (u32 i = 0; i < count; i++)
    {
        Mel_Layout_Item* it = &items[i];

        i32 pm, pc;
        preferred_axes(it, v, &pm, &pc);

        i32 ml = margin_main_lead(&it->spec, v);
        i32 mt = margin_main_trail(&it->spec, v);
        i32 cl = margin_cross_lead(&it->spec, v);
        i32 ct = margin_cross_trail(&it->spec, v);

        i32 extra = 0;
        if (total_weight > 0 && it->spec.weight > 0)
            extra = (leftover * it->spec.weight) / total_weight;

        i32 child_main = pm + extra;
        i32 main_pos = cursor + ml;

        u8  align = resolve_cross(it->spec.cross_align, lin->cross_align);
        i32 avail_cross = container_cross - cl - ct;
        i32 child_cross = pc;
        i32 cross_pos = lin->margin + cl;

        switch (align)
        {
        case MEL_ALIGN_STRETCH:
            child_cross = avail_cross;
            break;
        case MEL_ALIGN_CENTER:
            cross_pos += (avail_cross - pc) / 2;
            break;
        case MEL_ALIGN_END:
            cross_pos += avail_cross - pc;
            break;
        case MEL_ALIGN_START:
        default:
            break;
        }

        it->x = v ? cross_pos : main_pos;
        it->y = v ? main_pos : cross_pos;
        it->w = v ? child_cross : child_main;
        it->h = v ? child_main : child_cross;

        cursor += ml + child_main + mt + lin->spacing;
    }
}

static const Mel_Layout_Class s_linear_class = {
    .name = { .data = (u8*)"linear", .len = 6 },
    .measure = linear_measure,
    .arrange = linear_arrange,
};

const Mel_Layout_Class* mel_linear_layout_class(void) { return &s_linear_class; }

void mel_linear_layout_init(Mel_Linear_Layout* layout, bool vertical, Mel_Linear_Layout_Opt opt)
{
    layout->base.cls = &s_linear_class;
    layout->vertical = vertical;
    layout->spacing = opt.spacing;
    layout->margin = opt.margin;
    layout->cross_align = opt.cross_align;
}
