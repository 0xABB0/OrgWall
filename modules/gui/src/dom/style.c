#include "web.h"

#include <color/rgba8.h>

// Every Mel_Style field maps to inline CSS. fg goes to accent-color on a
// range input (the track/thumb tint is the slider's foreground); everywhere
// else it is the text color — on the checkbox that is the <label> wrapper, so
// text styles cover the caption and accent-color inherits into the box.
EM_JS(void, mel_web__style_apply, (int id, const char* family, double font_size, int weight, int italic, int has_fg, u32 fg, int has_bg, u32 bg, double border_w, int has_border, u32 border, double radius, int pl, int pt, int pr, int pb), {
    const el = MelWeb.els[id];
    if (!el)
        return;
    const fam = UTF8ToString(family);
    if (fam)
        el.style.fontFamily = fam;
    if (font_size > 0)
        el.style.fontSize = font_size + 'px';
    if (weight > 0)
        el.style.fontWeight = '' + weight;
    if (italic)
        el.style.fontStyle = 'italic';
    if (has_fg)
    {
        const c = MelWeb.css(fg);
        if (el.type === 'range')
            el.style.accentColor = c;
        else
        {
            el.style.color = c;
            el.style.accentColor = c;
        }
    }
    if (has_bg)
        el.style.background = MelWeb.css(bg);
    if (border_w > 0)
    {
        el.style.borderWidth = border_w + 'px';
        el.style.borderStyle = 'solid';
        if (has_border)
            el.style.borderColor = MelWeb.css(border);
    }
    if (radius > 0)
        el.style.borderRadius = radius + 'px';
    if (pl)
        el.style.paddingLeft = pl + 'px';
    if (pt)
        el.style.paddingTop = pt + 'px';
    if (pr)
        el.style.paddingRight = pr + 'px';
    if (pb)
        el.style.paddingBottom = pb + 'px';
});

void mel_gui_set_style(Mel_Gui_Handle h, Mel_Style style)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n)
        return;
    int id = mel_web__id_of(n);
    if (!id)
        return;
    char fam[256];
    mel_web__cstr(style.font_family, fam, sizeof fam);
    mel_web__style_apply(id,
                         fam,
                         style.font_size,
                         style.font_weight,
                         style.italic,
                         style.fg.set,
                         mel_color8_to_u32(style.fg.color),
                         style.bg.set,
                         mel_color8_to_u32(style.bg.color),
                         style.border_width,
                         style.border_color.set,
                         mel_color8_to_u32(style.border_color.color),
                         style.corner_radius,
                         style.padding_l,
                         style.padding_t,
                         style.padding_r,
                         style.padding_b);
}
