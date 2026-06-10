#include "linux.h"

#include <log/log.h>

static bool g_warned_style;

static void warn_style_once(void)
{
    if (g_warned_style)
        return;
    g_warned_style = true;
    mel_log_warn("gui", "xcb backend: only background color is stylable (XCB core draws no text, borders or rounded corners); other style fields are ignored");
}

static void style_surface(Mel_Gui_Handle h, const Mel_Style_Surface* s, bool extras)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native)
        return;

    Mel_Xcb_State* x = mel_gui__xcb();
    if (!x)
        return;

    if (extras || s->border_color.set || s->border_width || s->corner_radius || s->padding_l || s->padding_t || s->padding_r || s->padding_b)
        warn_style_once();

    if (!s->bg.set)
        return;

    /* Pixel composition assumes a TrueColor visual, like the rest of this
     * backend (the root window's depth-24 visual). */
    u32 pixel = ((u32)s->bg.color.r << 16) | ((u32)s->bg.color.g << 8) | (u32)s->bg.color.b;
    u32 values[] = { pixel };
    x->api.change_window_attributes(x->conn, (mel_xcb_window)(uintptr_t)n->native, MEL_XCB_CW_BACK_PIXEL, values);
    x->api.flush(x->conn);
}

void mel_label_set_style_opt(Mel_Gui_Handle h, Mel_Label_Style style) { style_surface(h, &style.surface, mel_font_any(&style.font) || style.fg.set); }

void mel_button_set_style_opt(Mel_Gui_Handle h, Mel_Button_Style style) { style_surface(h, &style.surface, mel_font_any(&style.font) || style.fg.set); }

void mel_checkbox_set_style_opt(Mel_Gui_Handle h, Mel_CheckBox_Style style) { style_surface(h, &style.surface, mel_font_any(&style.font) || style.fg.set || style.tint.set); }

void mel_textfield_set_style_opt(Mel_Gui_Handle h, Mel_TextField_Style style) { style_surface(h, &style.surface, mel_font_any(&style.font) || style.fg.set); }

void mel_slider_set_style_opt(Mel_Gui_Handle h, Mel_Slider_Style style) { style_surface(h, &style.surface, style.track.set || style.thumb.set); }

void mel_groupbox_set_style_opt(Mel_Gui_Handle h, Mel_GroupBox_Style style) { style_surface(h, &style.surface, mel_font_any(&style.title_font) || style.title_fg.set); }

void mel_splitter_set_style_opt(Mel_Gui_Handle h, Mel_Splitter_Style style) { style_surface(h, &style.surface, style.divider.set); }

void mel_splitpane_set_style_opt(Mel_Gui_Handle h, Mel_SplitPane_Style style) { style_surface(h, &style.surface, false); }

void mel_panel_set_style_opt(Mel_Gui_Handle h, Mel_Panel_Style style) { style_surface(h, &style.surface, false); }

void mel_canvas_set_style_opt(Mel_Gui_Handle h, Mel_Canvas_Style style) { style_surface(h, &style.surface, false); }

void mel_scrollview_set_style_opt(Mel_Gui_Handle h, Mel_ScrollView_Style style) { style_surface(h, &style.surface, false); }

void mel_tabview_set_style_opt(Mel_Gui_Handle h, Mel_TabView_Style style) { style_surface(h, &style.surface, false); }

void mel_tab_set_style_opt(Mel_Gui_Handle h, Mel_Tab_Style style) { style_surface(h, &style.surface, false); }

void mel_frame_set_style_opt(Mel_Gui_Handle h, Mel_Frame_Style style) { style_surface(h, &style.surface, false); }

void mel_dialog_set_style_opt(Mel_Gui_Handle h, Mel_Dialog_Style style) { style_surface(h, &style.surface, false); }
