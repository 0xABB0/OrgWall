#include "web.h"

#include <color/rgba8.h>

EM_JS(void, mel_web__style_font, (int id, const char* family, double size, int weight, int italic), {
    const el = MelWeb.els[id];
    if (!el)
        return;
    const fam = UTF8ToString(family);
    if (fam)
        el.style.fontFamily = fam;
    if (size > 0)
        el.style.fontSize = size + 'px';
    if (weight > 0)
        el.style.fontWeight = '' + weight;
    if (italic)
        el.style.fontStyle = 'italic';
});

EM_JS(void, mel_web__style_color, (int id, u32 c), {
    const el = MelWeb.els[id];
    if (el)
        el.style.color = MelWeb.css(c);
});

EM_JS(void, mel_web__style_accent, (int id, u32 c), {
    const el = MelWeb.els[id];
    if (el)
        el.style.accentColor = MelWeb.css(c);
});

EM_JS(void, mel_web__style_input_accent, (int id, u32 c), {
    const el = MelWeb.els[id];
    const i = el && el.querySelector('input');
    if (i)
        i.style.accentColor = MelWeb.css(c);
});

EM_JS(void, mel_web__style_legend_font, (int id, const char* family, double size, int weight, int italic), {
    const el = MelWeb.els[id];
    const lg = el && el.querySelector('legend');
    if (!lg)
        return;
    const fam = UTF8ToString(family);
    if (fam)
        lg.style.fontFamily = fam;
    if (size > 0)
        lg.style.fontSize = size + 'px';
    if (weight > 0)
        lg.style.fontWeight = '' + weight;
    if (italic)
        lg.style.fontStyle = 'italic';
});

EM_JS(void, mel_web__style_legend_color, (int id, u32 c), {
    const el = MelWeb.els[id];
    const lg = el && el.querySelector('legend');
    if (lg)
        lg.style.color = MelWeb.css(c);
});

EM_JS(void, mel_web__style_surface, (int id, int has_bg, u32 bg, double border_w, int has_border, u32 border, double radius, int pl, int pt, int pr, int pb), {
    const el = MelWeb.els[id];
    if (!el)
        return;
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

static int mel_web__style_id(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    return n ? mel_web__id_of(n) : 0;
}

static void mel_web__apply_font(int id, const Mel_Font* f)
{
    if (!mel_font_any(f))
        return;
    char fam[256];
    mel_web__cstr(f->family, fam, sizeof fam);
    mel_web__style_font(id, fam, f->size, f->weight, f->italic);
}

static void mel_web__apply_color(int id, Mel_Style_Color c)
{
    if (c.set)
        mel_web__style_color(id, mel_color8_to_u32(c.color));
}

static void mel_web__apply_surface(int id, const Mel_Style_Surface* s)
{
    if (!mel_style_surface_any(s))
        return;
    mel_web__style_surface(id, s->bg.set, mel_color8_to_u32(s->bg.color), s->border_width, s->border_color.set, mel_color8_to_u32(s->border_color.color), s->corner_radius, s->padding_l, s->padding_t, s->padding_r, s->padding_b);
}

void mel_label_set_style_opt(Mel_Gui_Handle h, Mel_Label_Style style)
{
    int id = mel_web__style_id(h);
    if (!id)
        return;
    mel_web__apply_font(id, &style.font);
    mel_web__apply_color(id, style.fg);
    mel_web__apply_surface(id, &style.surface);
}

void mel_button_set_style_opt(Mel_Gui_Handle h, Mel_Button_Style style)
{
    int id = mel_web__style_id(h);
    if (!id)
        return;
    mel_web__apply_font(id, &style.font);
    mel_web__apply_color(id, style.fg);
    mel_web__apply_surface(id, &style.surface);
}

void mel_textfield_set_style_opt(Mel_Gui_Handle h, Mel_TextField_Style style)
{
    int id = mel_web__style_id(h);
    if (!id)
        return;
    mel_web__apply_font(id, &style.font);
    mel_web__apply_color(id, style.fg);
    mel_web__apply_surface(id, &style.surface);
}

void mel_checkbox_set_style_opt(Mel_Gui_Handle h, Mel_CheckBox_Style style)
{
    int id = mel_web__style_id(h);
    if (!id)
        return;
    mel_web__apply_font(id, &style.font);
    mel_web__apply_color(id, style.fg);
    if (style.tint.set)
        mel_web__style_input_accent(id, mel_color8_to_u32(style.tint.color));
    mel_web__apply_surface(id, &style.surface);
}

void mel_slider_set_style_opt(Mel_Gui_Handle h, Mel_Slider_Style style)
{
    int id = mel_web__style_id(h);
    if (!id)
        return;
    if (style.track.set)
        mel_web__style_accent(id, mel_color8_to_u32(style.track.color));
    mel_web__apply_surface(id, &style.surface);
}

void mel_groupbox_set_style_opt(Mel_Gui_Handle h, Mel_GroupBox_Style style)
{
    int id = mel_web__style_id(h);
    if (!id)
        return;
    if (mel_font_any(&style.title_font))
    {
        char fam[256];
        mel_web__cstr(style.title_font.family, fam, sizeof fam);
        mel_web__style_legend_font(id, fam, style.title_font.size, style.title_font.weight, style.title_font.italic);
    }
    if (style.title_fg.set)
        mel_web__style_legend_color(id, mel_color8_to_u32(style.title_fg.color));
    mel_web__apply_surface(id, &style.surface);
}

void mel_splitter_set_style_opt(Mel_Gui_Handle h, Mel_Splitter_Style style)
{
    int id = mel_web__style_id(h);
    if (!id)
        return;
    mel_web__apply_surface(id, &style.surface);
}

void mel_splitpane_set_style_opt(Mel_Gui_Handle h, Mel_SplitPane_Style style)
{
    int id = mel_web__style_id(h);
    if (!id)
        return;
    mel_web__apply_surface(id, &style.surface);
}

void mel_panel_set_style_opt(Mel_Gui_Handle h, Mel_Panel_Style style)
{
    int id = mel_web__style_id(h);
    if (!id)
        return;
    mel_web__apply_surface(id, &style.surface);
}

void mel_canvas_set_style_opt(Mel_Gui_Handle h, Mel_Canvas_Style style)
{
    int id = mel_web__style_id(h);
    if (!id)
        return;
    mel_web__apply_surface(id, &style.surface);
}

void mel_scrollview_set_style_opt(Mel_Gui_Handle h, Mel_ScrollView_Style style)
{
    int id = mel_web__style_id(h);
    if (!id)
        return;
    mel_web__apply_surface(id, &style.surface);
}

void mel_frame_set_style_opt(Mel_Gui_Handle h, Mel_Frame_Style style)
{
    int id = mel_web__style_id(h);
    if (!id)
        return;
    mel_web__apply_surface(id, &style.surface);
}

void mel_dialog_set_style_opt(Mel_Gui_Handle h, Mel_Dialog_Style style)
{
    int id = mel_web__style_id(h);
    if (!id)
        return;
    mel_web__apply_surface(id, &style.surface);
}

void mel_tabview_set_style_opt(Mel_Gui_Handle h, Mel_TabView_Style style)
{
    int id = mel_web__style_id(h);
    if (!id)
        return;
    mel_web__apply_surface(id, &style.surface);
}

void mel_tab_set_style_opt(Mel_Gui_Handle h, Mel_Tab_Style style)
{
    int id = mel_web__style_id(h);
    if (!id)
        return;
    mel_web__apply_surface(id, &style.surface);
}
