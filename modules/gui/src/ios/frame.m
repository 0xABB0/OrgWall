#include "uikit.h"

static UIWindow*              g_window;
static UINavigationController* g_nav;

UINavigationController* mel_gui__ios_nav(void) { return g_nav; }

static void ios_ensure_nav(void)
{
    if (g_nav) return;
    g_nav    = [[UINavigationController alloc] init];
    g_window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    g_window.rootViewController = g_nav;
    [g_window makeKeyAndVisible];
}

@implementation MelViewController

- (void)loadView
{
    [super loadView];
    self.view.backgroundColor = [UIColor systemBackgroundColor];

    UIView* c = [[UIView alloc] init];
    c.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:c];
    self.content = c;

    // PAD (the default) confines content to the safe area; EDGE_TO_EDGE fills
    // the whole view and leaves the insets to the app.
    id top, lead, trail, bot;
    if (self.inset_mode == MEL_FRAME_EDGE_TO_EDGE) {
        top = self.view.topAnchor;      lead = self.view.leadingAnchor;
        trail = self.view.trailingAnchor; bot = self.view.bottomAnchor;
    } else {
        UILayoutGuide* g = self.view.safeAreaLayoutGuide;
        top = g.topAnchor;   lead = g.leadingAnchor;
        trail = g.trailingAnchor; bot = g.bottomAnchor;
    }
    [NSLayoutConstraint activateConstraints:@[
        [c.topAnchor      constraintEqualToAnchor:top],
        [c.leadingAnchor  constraintEqualToAnchor:lead],
        [c.trailingAnchor constraintEqualToAnchor:trail],
        [c.bottomAnchor   constraintEqualToAnchor:bot],
    ]];
}

- (void)viewDidLayoutSubviews
{
    [super viewDidLayoutSubviews];
    CGSize sz = self.content.bounds.size;
    i32 w = (i32)sz.width, h = (i32)sz.height;
    if (w == self.last_w && h == self.last_h) return;
    self.last_w = w; self.last_h = h;

    // Drive the gui layout with the real content size, then report insets so
    // EDGE_TO_EDGE apps can react.
    mel_gui__resized(self.frame_handle, w, h);
    mel_gui__layout_arrange(self.frame_handle);

    if (self.insets_cb.on_insets_changed) {
        UIEdgeInsets s = self.view.safeAreaInsets;
        Mel_Insets safe = { (i32)s.left, (i32)s.top, (i32)s.right, (i32)s.bottom };
        Mel_Frame_Insets in = { .safe_area = safe, .system_bars = safe };
        self.insets_cb.on_insets_changed(self.frame_handle, &in, mel_gui_user(self.frame_handle));
    }
}

@end

void mel_gui__ios_show_frame(Mel_Gui_Node* n)
{
    if (!n || !n->native) return;
    ios_ensure_nav();
    UIViewController* vc = (__bridge UIViewController*)n->native;
    if (g_nav.viewControllers.count == 0) {
        [g_nav setViewControllers:@[vc] animated:NO];
    } else if (g_nav.topViewController == vc) {
        // already shown
    } else if ([g_nav.viewControllers containsObject:vc]) {
        [g_nav popToViewController:vc animated:YES];
    } else {
        [g_nav pushViewController:vc animated:YES];
    }
}

Mel_Gui_Handle mel_frame_create_opt(Mel_Frame_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(MEL_GUI_HANDLE_NONE, o.x, o.y, o.w, o.h, 0, o.user,
                                         o.initial_state == MEL_FRAME_HIDDEN, NULL, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;
    NSString* title = mel_gui__ios_nsstring(o.title);

    ios_ensure_nav();
    MelViewController* vc = [[MelViewController alloc] init];
    vc.frame_handle = h;
    vc.inset_mode   = o.inset_mode;
    vc.insets_cb    = o.insets;
    vc.title        = title;
    (void)vc.view;  // force loadView so the content view exists for children

    n->native  = (void*)CFBridgingRetain(vc);
    n->content = (void*)CFBridgingRetain(vc.content);
    mel_gui__frames_inc();

    n->x = 0;
    n->y = 0;
    return h;
}

void mel_gui__nav_replace(Mel_Gui_Handle next, Mel_Gui_Handle prev)
{
    (void)prev;
    mel_gui__ios_show_frame(mel_gui__node(next));
}

void mel_gui__nav_back(Mel_Gui_Handle prev, Mel_Gui_Handle cur)
{
    (void)cur;
    if (!g_nav) return;
    Mel_Gui_Node* p = mel_gui__node(prev);
    if (p && p->native && [g_nav.viewControllers containsObject:(__bridge UIViewController*)p->native])
        [g_nav popToViewController:(__bridge UIViewController*)p->native animated:YES];
    else
        [g_nav popViewControllerAnimated:YES];
}

Mel_Frame_Insets mel_frame_insets(Mel_Gui_Handle h)
{
    Mel_Frame_Insets out = {0};
    Mel_Gui_Node* n = mel_gui__node(mel_gui__toplevel(h));
    if (!n || !n->native) return out;
    UIViewController* vc = (__bridge UIViewController*)n->native;
    UIEdgeInsets s = vc.view.safeAreaInsets;
    Mel_Insets safe = { (i32)s.left, (i32)s.top, (i32)s.right, (i32)s.bottom };
    out.safe_area   = safe;
    out.system_bars = safe;
    return out;
}
