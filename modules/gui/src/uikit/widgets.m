#include "uikit.h"

// ---------------------------------------------------------------------------
// Class implementations
// ---------------------------------------------------------------------------

@implementation MelView
- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    (void)event;
    CGPoint p = [touches.anyObject locationInView:self];
    if (self.pointer.on_pointer_down)
        self.pointer.on_pointer_down(self.handle, (i32)p.x, (i32)p.y, mel_gui_user(self.handle));
}
- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    (void)event;
    CGPoint p = [touches.anyObject locationInView:self];
    if (self.pointer.on_pointer_up)
        self.pointer.on_pointer_up(self.handle, (i32)p.x, (i32)p.y, mel_gui_user(self.handle));
    if (self.pointer.on_click)
        self.pointer.on_click(self.handle, mel_gui_user(self.handle));
}
@end

@implementation MelButton
- (void)melTapped { if (self.pointer.on_click) self.pointer.on_click(self.handle, mel_gui_user(self.handle)); }
@end

@implementation MelLabel
@end

@implementation MelSwitch
- (void)melToggled { if (self.on_.on_toggled) self.on_.on_toggled(self.handle, self.isOn, mel_gui_user(self.handle)); }
@end

@implementation MelSlider
- (void)melChanged { if (self.on_.on_value_changed) self.on_.on_value_changed(self.handle, (i32)self.value, mel_gui_user(self.handle)); }
@end

@implementation MelField
- (void)melChanged
{
    if (!self.on_.on_text_changed) return;
    const char* c = self.text.UTF8String;
    str8 t = { (u8*)c, c ? (size)strlen(c) : 0 };
    self.on_.on_text_changed(self.handle, t, mel_gui_user(self.handle));
}
@end

// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------

Mel_Gui_Handle mel_button_create_opt(Mel_Gui_Handle parent, Mel_Button_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden, &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;
    NSString* title = mel_gui__ios_nsstring(o.text);
    MelButton* b = [MelButton buttonWithType:UIButtonTypeSystem];
    b.frame   = CGRectMake(0, 0, n->width, n->height);
    b.handle  = h;
    b.pointer = o.pointer;
    [b setTitle:title forState:UIControlStateNormal];
    b.enabled = !o.disabled;
    [b addTarget:b action:@selector(melTapped) forControlEvents:UIControlEventTouchUpInside];
    mel_gui__ios_install_child(n, b);
    return h;
}

Mel_Gui_Handle mel_label_create_opt(Mel_Gui_Handle parent, Mel_Label_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden, &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;
    NSString* text = mel_gui__ios_nsstring(o.text);
    MelLabel* l = [[MelLabel alloc] initWithFrame:CGRectMake(0, 0, n->width, n->height)];
    l.handle        = h;
    l.text          = text;
    l.numberOfLines = 0;
    mel_gui__ios_install_child(n, l);
    return h;
}

Mel_Gui_Handle mel_checkbox_create_opt(Mel_Gui_Handle parent, Mel_CheckBox_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden, &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;
    MelSwitch* s = [[MelSwitch alloc] initWithFrame:CGRectMake(0, 0, n->width, n->height)];
    s.handle  = h;
    s.on_     = o.on_;
    s.enabled = !o.disabled;
    [s setOn:o.checked];
    [s addTarget:s action:@selector(melToggled) forControlEvents:UIControlEventValueChanged];
    mel_gui__ios_install_child(n, s);
    return h;
}

bool mel_checkbox_checked(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native) return false;
    id obj = (__bridge id)n->native;
    return [obj isKindOfClass:[UISwitch class]] ? ((UISwitch*)obj).isOn : false;
}

Mel_Gui_Handle mel_slider_create_opt(Mel_Gui_Handle parent, Mel_Slider_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden, &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;
    MelSlider* s = [[MelSlider alloc] initWithFrame:CGRectMake(0, 0, n->width, n->height)];
    s.handle       = h;
    s.on_          = o.on_;
    s.minimumValue = o.min_value;
    s.maximumValue = o.max_value;
    s.value        = o.value;
    s.enabled      = !o.disabled;
    [s addTarget:s action:@selector(melChanged) forControlEvents:UIControlEventValueChanged];
    mel_gui__ios_install_child(n, s);
    return h;
}

i32 mel_slider_value(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native) return 0;
    id obj = (__bridge id)n->native;
    return [obj isKindOfClass:[UISlider class]] ? (i32)((UISlider*)obj).value : 0;
}

void mel_slider_set_value(Mel_Gui_Handle h, i32 value)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native) return;
    id obj = (__bridge id)n->native;
    if ([obj isKindOfClass:[UISlider class]]) ((UISlider*)obj).value = value;
}

Mel_Gui_Handle mel_textfield_create_opt(Mel_Gui_Handle parent, Mel_TextField_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden, &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;
    NSString* text = mel_gui__ios_nsstring(o.text);
    MelField* f = [[MelField alloc] initWithFrame:CGRectMake(0, 0, n->width, n->height)];
    f.handle        = h;
    f.on_           = o.on_;
    f.focus         = o.focus;
    f.text          = text;
    f.borderStyle   = UITextBorderStyleRoundedRect;
    f.enabled       = !o.disabled;
    [f addTarget:f action:@selector(melChanged) forControlEvents:UIControlEventEditingChanged];
    mel_gui__ios_install_child(n, f);
    return h;
}

// ---------------------------------------------------------------------------
// Containers (barebone: plain views that host children)
// ---------------------------------------------------------------------------

static Mel_Gui_Handle make_container(Mel_Gui_Handle parent, i32 x, i32 y, i32 w, i32 hh,
                                     u32 id, void* user, bool hidden,
                                     const Mel_Layoutable* lo, Mel_Layout* layout,
                                     Mel_Gui_Pointer_Cb pointer, Mel_Gui_Focus_Cb focus)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, x, y, w, hh, id, user, hidden, lo, layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;
    MelView* v = [[MelView alloc] initWithFrame:CGRectMake(0, 0, n->width, n->height)];
    v.handle  = h;
    v.pointer = pointer;
    v.focus   = focus;
    mel_gui__ios_install_child(n, v);
    return h;
}

Mel_Gui_Handle mel_panel_create_opt(Mel_Gui_Handle parent, Mel_Panel_Opt o)
{
    return make_container(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                          &o.layoutable, o.layout, o.pointer, o.focus);
}

Mel_Gui_Handle mel_groupbox_create_opt(Mel_Gui_Handle parent, Mel_GroupBox_Opt o)
{
    return make_container(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                          &o.layoutable, o.layout, (Mel_Gui_Pointer_Cb){0}, o.focus);
}

Mel_Gui_Handle mel_scrollview_create_opt(Mel_Gui_Handle parent, Mel_ScrollView_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden, &o.layoutable, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;
    i32 cw = o.content_w, ch = o.content_h;
    UIScrollView* sv = [[UIScrollView alloc] initWithFrame:CGRectMake(0, 0, n->width, n->height)];
    sv.contentSize = CGSizeMake(cw > 0 ? cw : n->width, ch > 0 ? ch : n->height);
    mel_gui__ios_install_child(n, sv);
    return h;
}

Mel_Gui_Handle mel_splitter_create_opt(Mel_Gui_Handle parent, Mel_Splitter_Opt o)
{
    return make_container(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                          &o.layoutable, NULL, (Mel_Gui_Pointer_Cb){0}, o.focus);
}

Mel_Gui_Handle mel_splitpane_create_opt(Mel_Gui_Handle splitter, Mel_SplitPane_Opt o)
{
    return make_container(splitter, 0, 0, 0, 0, o.id, o.user, false,
                          &o.layoutable, o.layout, (Mel_Gui_Pointer_Cb){0}, (Mel_Gui_Focus_Cb){0});
}

Mel_Gui_Handle mel_tabview_create_opt(Mel_Gui_Handle parent, Mel_TabView_Opt o)
{
    return make_container(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                          &o.layoutable, NULL, (Mel_Gui_Pointer_Cb){0}, o.focus);
}

Mel_Gui_Handle mel_tab_create_opt(Mel_Gui_Handle tabview, Mel_Tab_Opt o)
{
    Mel_Gui_Node* tv = mel_gui__node(tabview);
    bool first = true;
    if (tv) {
        u32 count = 0;
        Mel_Gui_Node* data = mel_gui__nodes(&count);
        for (u32 i = 0; i < count; i++)
            if (mel_gui_handle_eq(data[i].parent, tabview)) { first = false; break; }
    }
    // Barebone: tabs stack; only the first is shown (no tab bar yet).
    return make_container(tabview, 0, 0, 0, 0, o.id, o.user, !first,
                          &o.layoutable, o.layout, (Mel_Gui_Pointer_Cb){0}, (Mel_Gui_Focus_Cb){0});
}

void mel_tabview_select(Mel_Gui_Handle tabview, i32 index)
{
    u32 count = 0;
    Mel_Gui_Node* data = mel_gui__nodes(&count);
    i32 seen = 0;
    for (u32 i = 0; i < count; i++) {
        if (!mel_gui_handle_eq(data[i].parent, tabview)) continue;
        mel_gui_set_visible(data[i].self, seen == index);
        seen++;
    }
}

i32 mel_tabview_selected(Mel_Gui_Handle tabview)
{
    u32 count = 0;
    Mel_Gui_Node* data = mel_gui__nodes(&count);
    i32 seen = 0;
    for (u32 i = 0; i < count; i++) {
        if (!mel_gui_handle_eq(data[i].parent, tabview)) continue;
        if (!data[i].hidden) return seen;
        seen++;
    }
    return 0;
}
