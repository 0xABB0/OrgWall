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

static bool ios_wants_font(const Mel_Style* s) { return s->font_family.len || s->font_size || s->font_weight || s->italic; }

// Every zero axis inherits from the widget's current font, so a lone
// font_size (or italic) does not silently reset weight or family.
static UIFont* ios_font(const Mel_Style* s, UIFont* current)
{
    CGFloat sz = s->font_size > 0 ? (CGFloat)s->font_size : (current ? current.pointSize : UIFont.systemFontSize);
    UIFont* f = nil;
    if (s->font_family.len)
        f = [UIFont fontWithName:mel_gui__ios_nsstring(s->font_family) size:sz];
    if (!f && s->font_weight)
        f = [UIFont systemFontOfSize:sz weight:ios_font_weight(s->font_weight)];
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

static void ios_surface(UIView* v, const Mel_Style* s)
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

void mel_gui_set_style(Mel_Gui_Handle h, Mel_Style style)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native)
        return;
    id obj = (__bridge id)n->native;
    if ([obj isKindOfClass:[UIViewController class]])
    {
        if (style.bg.set)
            ((UIViewController*)obj).view.backgroundColor = ios_color(style.bg.color);
        return;
    }
    if (![obj isKindOfClass:[UIView class]])
        return;
    UIView* v = (UIView*)obj;

    if ([v isKindOfClass:[UIButton class]])
    {
        UIButton* b = (UIButton*)v;
        if (style.fg.set)
            [b setTitleColor:ios_color(style.fg.color) forState:UIControlStateNormal];
        if (ios_wants_font(&style))
            b.titleLabel.font = ios_font(&style, b.titleLabel.font);
        // UIButtonConfiguration needs iOS 15; the build's floor is 13.
        if (style.padding_l || style.padding_t || style.padding_r || style.padding_b)
            b.contentEdgeInsets = UIEdgeInsetsMake(style.padding_t, style.padding_l, style.padding_b, style.padding_r);
        ios_surface(v, &style);
        return;
    }
    if ([v isKindOfClass:[UILabel class]])
    {
        UILabel* l = (UILabel*)v;
        if (style.fg.set)
            l.textColor = ios_color(style.fg.color);
        if (ios_wants_font(&style))
            l.font = ios_font(&style, l.font);
        ios_surface(v, &style);
        return;
    }
    if ([v isKindOfClass:[UITextField class]])
    {
        UITextField* f = (UITextField*)v;
        if (style.fg.set)
            f.textColor = ios_color(style.fg.color);
        if (ios_wants_font(&style))
            f.font = ios_font(&style, f.font);
        // The native bezel and a layer border would stack; drop the bezel
        // only when the style draws its own edge.
        if (style.border_color.set || style.border_width > 0 || style.corner_radius > 0)
            f.borderStyle = UITextBorderStyleNone;
        ios_surface(v, &style);
        return;
    }
    if ([v isKindOfClass:[UISwitch class]])
    {
        if (style.fg.set)
            ((UISwitch*)v).onTintColor = ios_color(style.fg.color);
        return;
    }
    if ([v isKindOfClass:[UISlider class]])
    {
        UISlider* s = (UISlider*)v;
        if (style.fg.set)
        {
            s.minimumTrackTintColor = ios_color(style.fg.color);
            s.thumbTintColor = ios_color(style.fg.color);
        }
        return;
    }
    ios_surface(v, &style);
}
