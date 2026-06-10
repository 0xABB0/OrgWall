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

static NSFont* style_resolve_font(const Mel_Font* f, NSFont* current)
{
    NSFontManager* fm = [NSFontManager sharedFontManager];
    CGFloat        pts = f->size > 0 ? (CGFloat)f->size : (current ? current.pointSize : [NSFont systemFontSize]);

    NSFont* font = nil;
    if (f->family.len)
    {
        font = [NSFont fontWithName:mel_gui__macos_nsstring(f->family) size:pts];
        if (font && f->weight)
        {
            font = [fm fontWithFamily:font.familyName traits:0 weight:style_manager_weight(f->weight) size:pts] ?: font;
        }
    }
    if (!font)
    {
        if (f->weight)
        {
            font = [NSFont systemFontOfSize:pts weight:style_system_weight(f->weight)];
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
    if (f->italic)
    {
        font = [fm convertFont:font toHaveTrait:NSItalicFontMask];
    }
    return font;
}

static void style_layer_border(NSView* view, const Mel_Style_Surface* s)
{
    if (!s->border_color.set && !s->border_width)
        return;
    view.wantsLayer = YES;
    if (s->border_width)
        view.layer.borderWidth = (CGFloat)s->border_width;
    if (s->border_color.set)
        view.layer.borderColor = style_color(s->border_color.color).CGColor;
}

static void style_layer_radius(NSView* view, const Mel_Style_Surface* s, bool mask)
{
    if (!s->corner_radius)
        return;
    view.wantsLayer = YES;
    view.layer.cornerRadius = (CGFloat)s->corner_radius;
    if (mask)
        view.layer.masksToBounds = YES;
}

static void style_layer_surface(NSView* view, const Mel_Style_Surface* s)
{
    if (s->bg.set)
    {
        view.wantsLayer = YES;
        view.layer.backgroundColor = style_color(s->bg.color).CGColor;
    }
    style_layer_border(view, s);
    style_layer_radius(view, s, false);
}

static id style_native(Mel_Gui_Handle h, Class cls)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native)
        return nil;
    id obj = (__bridge id)n->native;
    return [obj isKindOfClass:cls] ? obj : nil;
}

static void style_apply_text(NSTextField* tf, const Mel_Font* font, Mel_Style_Color fg, const Mel_Style_Surface* s)
{
    if (mel_font_any(font))
        tf.font = style_resolve_font(font, tf.font);
    if (fg.set)
        tf.textColor = style_color(fg.color);
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

static void style_apply_button(NSButton* button, const Mel_Font* font, Mel_Style_Color fg, const Mel_Style_Surface* s)
{
    if (mel_font_any(font))
        button.font = style_resolve_font(font, button.font);
    if (fg.set)
        style_button_fg(button, style_color(fg.color));
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

void mel_label_set_style_opt(Mel_Gui_Handle h, Mel_Label_Style style)
{
    @autoreleasepool
    {
        MelGuiLabel* label = style_native(h, [MelGuiLabel class]);
        if (!label)
            return;
        style_apply_text(label, &style.font, style.fg, &style.surface);
    }
}

void mel_textfield_set_style_opt(Mel_Gui_Handle h, Mel_TextField_Style style)
{
    @autoreleasepool
    {
        MelGuiTextField* tf = style_native(h, [MelGuiTextField class]);
        if (!tf)
            return;
        style_apply_text(tf, &style.font, style.fg, &style.surface);
    }
}

void mel_button_set_style_opt(Mel_Gui_Handle h, Mel_Button_Style style)
{
    @autoreleasepool
    {
        MelGuiButton* button = style_native(h, [MelGuiButton class]);
        if (!button)
            return;
        if (style.fg.set)
            button.style_fg = style_color(style.fg.color);
        style_apply_button(button, &style.font, style.fg, &style.surface);
    }
}

void mel_checkbox_set_style_opt(Mel_Gui_Handle h, Mel_CheckBox_Style style)
{
    @autoreleasepool
    {
        MelGuiCheckBox* box = style_native(h, [MelGuiCheckBox class]);
        if (!box)
            return;
        if (style.fg.set)
            box.style_fg = style_color(style.fg.color);
        style_apply_button(box, &style.font, style.fg, &style.surface);
        /* tint: the check glyph has no color API on NSButton; honest gap. */
    }
}

void mel_slider_set_style_opt(Mel_Gui_Handle h, Mel_Slider_Style style)
{
    @autoreleasepool
    {
        MelGuiSlider* slider = style_native(h, [MelGuiSlider class]);
        if (!slider)
            return;
        if (style.track.set)
        {
            if (@available(macOS 10.12.2, *))
                slider.trackFillColor = style_color(style.track.color);
        }
        /* thumb: no NSSlider knob-color API; honest gap. */
        style_layer_surface(slider, &style.surface);
    }
}

void mel_groupbox_set_style_opt(Mel_Gui_Handle h, Mel_GroupBox_Style style)
{
    @autoreleasepool
    {
        NSBox* box = style_native(h, [NSBox class]);
        if (!box)
            return;
        const Mel_Style_Surface* s = &style.surface;
        if (mel_font_any(&style.title_font))
            box.titleFont = style_resolve_font(&style.title_font, box.titleFont);
        /* title_fg: NSBox exposes no title color; honest gap. */
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
}

void mel_scrollview_set_style_opt(Mel_Gui_Handle h, Mel_ScrollView_Style style)
{
    @autoreleasepool
    {
        NSScrollView* scroll = style_native(h, [NSScrollView class]);
        if (!scroll)
            return;
        if (style.surface.bg.set)
        {
            scroll.drawsBackground = YES;
            scroll.backgroundColor = style_color(style.surface.bg.color);
        }
        style_layer_border(scroll, &style.surface);
        style_layer_radius(scroll, &style.surface, true);
    }
}

void mel_splitter_set_style_opt(Mel_Gui_Handle h, Mel_Splitter_Style style)
{
    @autoreleasepool
    {
        MelGuiSplitView* sv = style_native(h, [MelGuiSplitView class]);
        if (!sv)
            return;
        style_layer_surface(sv, &style.surface);
        /* divider: NSSplitView dividerColor is read-only; honest gap. */
    }
}

void mel_splitpane_set_style_opt(Mel_Gui_Handle h, Mel_SplitPane_Style style)
{
    @autoreleasepool
    {
        MelGuiContainerView* host = style_native(h, [MelGuiContainerView class]);
        if (!host)
            return;
        style_layer_surface(host, &style.surface);
    }
}

void mel_panel_set_style_opt(Mel_Gui_Handle h, Mel_Panel_Style style)
{
    @autoreleasepool
    {
        MelGuiContainerView* view = style_native(h, [MelGuiContainerView class]);
        if (!view)
            return;
        style_layer_surface(view, &style.surface);
    }
}

void mel_canvas_set_style_opt(Mel_Gui_Handle h, Mel_Canvas_Style style)
{
    @autoreleasepool
    {
        MelGuiCanvasView* view = style_native(h, [MelGuiCanvasView class]);
        if (!view)
            return;
        style_layer_surface(view, &style.surface);
    }
}

void mel_tabview_set_style_opt(Mel_Gui_Handle h, Mel_TabView_Style style)
{
    @autoreleasepool
    {
        MelGuiTabView* tv = style_native(h, [MelGuiTabView class]);
        if (!tv)
            return;
        style_layer_surface(tv, &style.surface);
    }
}

void mel_tab_set_style_opt(Mel_Gui_Handle h, Mel_Tab_Style style)
{
    @autoreleasepool
    {
        MelGuiContainerView* host = style_native(h, [MelGuiContainerView class]);
        if (!host)
            return;
        style_layer_surface(host, &style.surface);
    }
}

void mel_frame_set_style_opt(Mel_Gui_Handle h, Mel_Frame_Style style)
{
    @autoreleasepool
    {
        NSWindow* window = style_native(h, [NSWindow class]);
        if (!window)
            return;
        /* only bg maps to a window natively; border/radius/padding: honest gap. */
        if (style.surface.bg.set)
            window.backgroundColor = style_color(style.surface.bg.color);
    }
}

void mel_dialog_set_style_opt(Mel_Gui_Handle h, Mel_Dialog_Style style)
{
    @autoreleasepool
    {
        NSWindow* window = style_native(h, [NSWindow class]);
        if (!window)
            return;
        if (style.surface.bg.set)
            window.backgroundColor = style_color(style.surface.bg.color);
    }
}
