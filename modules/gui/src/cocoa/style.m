#include "macos.h"

static NSColor* style_color(mel_color8 c) { return [NSColor colorWithSRGBRed:c.r / 255.0 green:c.g / 255.0 blue:c.b / 255.0 alpha:c.a / 255.0]; }

static NSFontWeight style_system_weight(u16 w)
{
    if (w <= 100)
        return NSFontWeightUltraLight;
    if (w <= 200)
        return NSFontWeightThin;
    if (w <= 300)
        return NSFontWeightLight;
    if (w <= 400)
        return NSFontWeightRegular;
    if (w <= 500)
        return NSFontWeightMedium;
    if (w <= 600)
        return NSFontWeightSemibold;
    if (w <= 700)
        return NSFontWeightBold;
    if (w <= 800)
        return NSFontWeightHeavy;
    return NSFontWeightBlack;
}

/* NSFontManager weight scale: 0..15, 5 normal, 9 bold. */
static NSInteger style_manager_weight(u16 w)
{
    if (w <= 100)
        return 2;
    if (w <= 200)
        return 3;
    if (w <= 300)
        return 4;
    if (w <= 400)
        return 5;
    if (w <= 500)
        return 6;
    if (w <= 600)
        return 8;
    if (w <= 700)
        return 9;
    if (w <= 800)
        return 10;
    return 12;
}

static bool style_wants_font(const Mel_Style* s) { return s->font_family.len || s->font_size || s->font_weight || s->italic; }

static NSFont* style_resolve_font(const Mel_Style* s, NSFont* current)
{
    NSFontManager* fm = [NSFontManager sharedFontManager];
    CGFloat        pts = s->font_size > 0 ? (CGFloat)s->font_size : (current ? current.pointSize : [NSFont systemFontSize]);

    NSFont* font = nil;
    if (s->font_family.len)
    {
        font = [NSFont fontWithName:mel_gui__macos_nsstring(s->font_family) size:pts];
        if (font && s->font_weight)
        {
            font = [fm fontWithFamily:font.familyName traits:0 weight:style_manager_weight(s->font_weight) size:pts] ?: font;
        }
    }
    if (!font)
    {
        if (s->font_weight)
        {
            font = [NSFont systemFontOfSize:pts weight:style_system_weight(s->font_weight)];
        }
        else if (current)
        {
            font = [fm convertFont:current toSize:pts];
        }
        else
        {
            font = [NSFont systemFontOfSize:pts];
        }
    }
    if (s->italic)
    {
        font = [fm convertFont:font toHaveTrait:NSItalicFontMask];
    }
    return font;
}

static void style_layer_border(NSView* view, const Mel_Style* s)
{
    if (!s->border_color.set && !s->border_width)
        return;
    view.wantsLayer = YES;
    if (s->border_width)
        view.layer.borderWidth = (CGFloat)s->border_width;
    if (s->border_color.set)
        view.layer.borderColor = style_color(s->border_color.color).CGColor;
}

static void style_layer_radius(NSView* view, const Mel_Style* s, bool mask)
{
    if (!s->corner_radius)
        return;
    view.wantsLayer = YES;
    view.layer.cornerRadius = (CGFloat)s->corner_radius;
    if (mask)
        view.layer.masksToBounds = YES;
}

static void style_apply_window(NSWindow* window, const Mel_Style* s)
{
    if (s->bg.set)
        window.backgroundColor = style_color(s->bg.color);
}

static void style_apply_box(NSBox* box, const Mel_Style* s)
{
    if (style_wants_font(s))
        box.titleFont = style_resolve_font(s, box.titleFont);
    if (s->bg.set || s->border_color.set || s->border_width || s->corner_radius)
        box.boxType = NSBoxCustom;
    if (s->bg.set)
        box.fillColor = style_color(s->bg.color);
    if (s->border_color.set)
        box.borderColor = style_color(s->border_color.color);
    if (s->border_width)
        box.borderWidth = (CGFloat)s->border_width;
    if (s->corner_radius)
        box.cornerRadius = (CGFloat)s->corner_radius;
    /* contentViewMargins is an NSSize: left/top only, mirrored right/bottom;
     * asymmetric padding is not expressible on NSBox. */
    if (s->padding_l || s->padding_t)
        box.contentViewMargins = NSMakeSize(s->padding_l, s->padding_t);
}

static void style_apply_textfield(NSTextField* tf, const Mel_Style* s)
{
    if (style_wants_font(s))
        tf.font = style_resolve_font(s, tf.font);
    if (s->fg.set)
        tf.textColor = style_color(s->fg.color);
    if (s->bg.set)
    {
        tf.drawsBackground = YES;
        tf.backgroundColor = style_color(s->bg.color);
    }
    style_layer_border(tf, s);
    style_layer_radius(tf, s, true);
}

static void style_button_fg(NSButton* button, NSColor* fg)
{
    NSMutableAttributedString* title = [button.attributedTitle mutableCopy];
    if (!title.length)
        return;
    [title addAttribute:NSForegroundColorAttributeName value:fg range:NSMakeRange(0, title.length)];
    button.attributedTitle = title;
}

void mel_gui__macos_button_reapply_fg(NSButton* button)
{
    NSColor* fg = nil;
    if ([button isKindOfClass:[MelGuiButton class]])
        fg = [(MelGuiButton*)button style_fg];
    else if ([button isKindOfClass:[MelGuiCheckBox class]])
        fg = [(MelGuiCheckBox*)button style_fg];
    if (fg)
        style_button_fg(button, fg);
}

static void style_apply_button(NSButton* button, const Mel_Style* s)
{
    if (style_wants_font(s))
        button.font = style_resolve_font(s, button.font);
    if (s->fg.set)
    {
        NSColor* fg = style_color(s->fg.color);
        if ([button isKindOfClass:[MelGuiButton class]])
            [(MelGuiButton*)button setStyle_fg:fg];
        else if ([button isKindOfClass:[MelGuiCheckBox class]])
            [(MelGuiCheckBox*)button setStyle_fg:fg];
        style_button_fg(button, fg);
    }
    /* bg/radius have no native bezel knob: drop the bezel and emulate just
     * the surface on the layer; an untouched button keeps the native bezel. */
    if (s->bg.set || s->corner_radius)
    {
        button.bordered = NO;
        button.wantsLayer = YES;
        if (s->bg.set)
            button.layer.backgroundColor = style_color(s->bg.color).CGColor;
        if (s->corner_radius)
            button.layer.cornerRadius = (CGFloat)s->corner_radius;
    }
    style_layer_border(button, s);
}

static void style_apply_slider(NSSlider* slider, const Mel_Style* s)
{
    if (s->fg.set)
    {
        if (@available(macOS 10.12.2, *))
            slider.trackFillColor = style_color(s->fg.color);
    }
}

static void style_apply_scrollview(NSScrollView* scroll, const Mel_Style* s)
{
    if (s->bg.set)
    {
        scroll.drawsBackground = YES;
        scroll.backgroundColor = style_color(s->bg.color);
    }
    style_layer_border(scroll, s);
    style_layer_radius(scroll, s, true);
}

static void style_apply_view(NSView* view, const Mel_Style* s)
{
    if (s->bg.set)
    {
        view.wantsLayer = YES;
        view.layer.backgroundColor = style_color(s->bg.color).CGColor;
    }
    style_layer_border(view, s);
    style_layer_radius(view, s, false);
}

void mel_gui_set_style(Mel_Gui_Handle h, Mel_Style style)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native)
        return;

    @autoreleasepool
    {
        id obj = (__bridge id)n->native;
        if ([obj isKindOfClass:[NSWindow class]])
        {
            style_apply_window((NSWindow*)obj, &style);
        }
        else if ([obj isKindOfClass:[NSBox class]])
        {
            style_apply_box((NSBox*)obj, &style);
        }
        else if ([obj isKindOfClass:[NSTextField class]])
        {
            style_apply_textfield((NSTextField*)obj, &style);
        }
        else if ([obj isKindOfClass:[NSButton class]])
        {
            style_apply_button((NSButton*)obj, &style);
        }
        else if ([obj isKindOfClass:[NSSlider class]])
        {
            style_apply_slider((NSSlider*)obj, &style);
        }
        else if ([obj isKindOfClass:[NSScrollView class]])
        {
            style_apply_scrollview((NSScrollView*)obj, &style);
        }
        else if ([obj isKindOfClass:[NSView class]])
        {
            style_apply_view((NSView*)obj, &style);
        }
    }
}
