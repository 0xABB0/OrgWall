#include "uikit.h"

static UIColor* ios_color(mel_color8 c) { return [UIColor colorWithRed:c.r / 255.0 green:c.g / 255.0 blue:c.b / 255.0 alpha:c.a / 255.0]; }

static UIFontWeight ios_font_weight(u16 w)
{
    if (w <= 100)
        return UIFontWeightUltraLight;
    if (w <= 200)
        return UIFontWeightThin;
    if (w <= 300)
        return UIFontWeightLight;
    if (w <= 400)
        return UIFontWeightRegular;
    if (w <= 500)
        return UIFontWeightMedium;
    if (w <= 600)
        return UIFontWeightSemibold;
    if (w <= 700)
        return UIFontWeightBold;
    if (w <= 800)
        return UIFontWeightHeavy;
    return UIFontWeightBlack;
}

// Every zero axis inherits from the widget's current font, so a lone
// size (or italic) does not silently reset weight or family.
static UIFont* ios_font(const Mel_Font* s, UIFont* current)
{
    CGFloat sz = s->size > 0 ? (CGFloat)s->size : (current ? current.pointSize : UIFont.systemFontSize);
    UIFont* f = nil;
    if (s->family.len)
        f = [UIFont fontWithName:mel_gui__ios_nsstring(s->family) size:sz];
    if (!f && s->weight)
        f = [UIFont systemFontOfSize:sz weight:ios_font_weight(s->weight)];
    if (!f)
        f = current ? [current fontWithSize:sz] : [UIFont systemFontOfSize:sz];
    if (s->italic)
    {
        UIFontDescriptor* d = [f.fontDescriptor fontDescriptorWithSymbolicTraits:f.fontDescriptor.symbolicTraits | UIFontDescriptorTraitItalic];
        if (d)
            f = [UIFont fontWithDescriptor:d size:sz];
    }
    return f;
}

static void ios_surface(UIView* v, const Mel_Style_Surface* s)
{
    if (s->bg.set)
        v.backgroundColor = ios_color(s->bg.color);
    if (s->border_color.set)
        v.layer.borderColor = ios_color(s->border_color.color).CGColor;
    if (s->border_width > 0)
        v.layer.borderWidth = s->border_width;
    if (s->corner_radius > 0)
    {
        v.layer.cornerRadius = s->corner_radius;
        v.clipsToBounds = YES;
    }
}

static UIView* ios_view(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native)
        return nil;
    id obj = (__bridge id)n->native;
    return [obj isKindOfClass:[UIView class]] ? (UIView*)obj : nil;
}

static void ios_view_surface(Mel_Gui_Handle h, const Mel_Style_Surface* s)
{
    UIView* v = ios_view(h);
    if (v)
        ios_surface(v, s);
}

static void ios_controller_surface(Mel_Gui_Handle h, const Mel_Style_Surface* s)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native)
        return;
    id obj = (__bridge id)n->native;
    if ([obj isKindOfClass:[UIViewController class]] && s->bg.set)
        ((UIViewController*)obj).view.backgroundColor = ios_color(s->bg.color);
}

void mel_label_set_style_opt(Mel_Gui_Handle h, Mel_Label_Style style)
{
    UIView* v = ios_view(h);
    if (![v isKindOfClass:[UILabel class]])
        return;
    UILabel* l = (UILabel*)v;
    if (style.fg.set)
        l.textColor = ios_color(style.fg.color);
    if (mel_font_any(&style.font))
        l.font = ios_font(&style.font, l.font);
    ios_surface(l, &style.surface);
}

void mel_button_set_style_opt(Mel_Gui_Handle h, Mel_Button_Style style)
{
    UIView* v = ios_view(h);
    if (![v isKindOfClass:[UIButton class]])
        return;
    UIButton* b = (UIButton*)v;
    if (style.fg.set)
        [b setTitleColor:ios_color(style.fg.color) forState:UIControlStateNormal];
    if (mel_font_any(&style.font))
        b.titleLabel.font = ios_font(&style.font, b.titleLabel.font);
    // UIButtonConfiguration needs iOS 15; the build's floor is 13.
    const Mel_Style_Surface* s = &style.surface;
    if (s->padding_l || s->padding_t || s->padding_r || s->padding_b)
        b.contentEdgeInsets = UIEdgeInsetsMake(s->padding_t, s->padding_l, s->padding_b, s->padding_r);
    ios_surface(b, s);
}

void mel_textfield_set_style_opt(Mel_Gui_Handle h, Mel_TextField_Style style)
{
    UIView* v = ios_view(h);
    if (![v isKindOfClass:[UITextField class]])
        return;
    UITextField* f = (UITextField*)v;
    if (style.fg.set)
        f.textColor = ios_color(style.fg.color);
    if (mel_font_any(&style.font))
        f.font = ios_font(&style.font, f.font);
    // The native bezel and a layer border would stack; drop the bezel
    // only when the style draws its own edge.
    if (style.surface.border_color.set || style.surface.border_width > 0 || style.surface.corner_radius > 0)
        f.borderStyle = UITextBorderStyleNone;
    ios_surface(f, &style.surface);
}

void mel_checkbox_set_style_opt(Mel_Gui_Handle h, Mel_CheckBox_Style style)
{
    UIView* v = ios_view(h);
    if (![v isKindOfClass:[UISwitch class]])
        return;
    if (style.tint.set)
        ((UISwitch*)v).onTintColor = ios_color(style.tint.color);
    ios_surface(v, &style.surface);
}

void mel_slider_set_style_opt(Mel_Gui_Handle h, Mel_Slider_Style style)
{
    UIView* v = ios_view(h);
    if (![v isKindOfClass:[UISlider class]])
        return;
    UISlider* s = (UISlider*)v;
    if (style.track.set)
        s.minimumTrackTintColor = ios_color(style.track.color);
    if (style.thumb.set)
        s.thumbTintColor = ios_color(style.thumb.color);
    ios_surface(s, &style.surface);
}

void mel_groupbox_set_style_opt(Mel_Gui_Handle h, Mel_GroupBox_Style style) { ios_view_surface(h, &style.surface); }

void mel_panel_set_style_opt(Mel_Gui_Handle h, Mel_Panel_Style style) { ios_view_surface(h, &style.surface); }

void mel_canvas_set_style_opt(Mel_Gui_Handle h, Mel_Canvas_Style style) { ios_view_surface(h, &style.surface); }

void mel_scrollview_set_style_opt(Mel_Gui_Handle h, Mel_ScrollView_Style style) { ios_view_surface(h, &style.surface); }

void mel_splitter_set_style_opt(Mel_Gui_Handle h, Mel_Splitter_Style style) { ios_view_surface(h, &style.surface); }

void mel_splitpane_set_style_opt(Mel_Gui_Handle h, Mel_SplitPane_Style style) { ios_view_surface(h, &style.surface); }

void mel_tabview_set_style_opt(Mel_Gui_Handle h, Mel_TabView_Style style) { ios_view_surface(h, &style.surface); }

void mel_tab_set_style_opt(Mel_Gui_Handle h, Mel_Tab_Style style) { ios_view_surface(h, &style.surface); }

void mel_frame_set_style_opt(Mel_Gui_Handle h, Mel_Frame_Style style) { ios_controller_surface(h, &style.surface); }

void mel_dialog_set_style_opt(Mel_Gui_Handle h, Mel_Dialog_Style style) { ios_controller_surface(h, &style.surface); }
