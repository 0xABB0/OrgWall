#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "../ui.native.listbox.h"

static const void* kListboxDelegateKey = &kListboxDelegateKey;
static const void* kListboxTableViewKey = &kListboxTableViewKey;

@interface MelListboxDelegate : NSObject <NSTableViewDataSource, NSTableViewDelegate>
@property (nonatomic, assign) Mel_NListbox* listbox;
@end

@implementation MelListboxDelegate

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    (void)tableView;
    if (!self.listbox)
        return 0;
    return (NSInteger)self.listbox->item_count;
}

- (id)tableView:(NSTableView*)tableView objectValueForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row
{
    (void)tableView;
    (void)tableColumn;
    if (!self.listbox)
        return @"";
    if (row < 0 || row >= (NSInteger)self.listbox->item_count)
        return @"";
    return [NSString stringWithUTF8String:self.listbox->items[row]];
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification
{
    NSTableView* tv = [notification object];
    if (!self.listbox)
        return;

    NSInteger row = [tv selectedRow];
    self.listbox->selected = (i32)row;

    if (self.listbox->on_select)
        self.listbox->on_select(self.listbox->selected, self.listbox->user_data);
}

@end

@interface MelListboxScrollView : NSView
@end

@implementation MelListboxScrollView

- (BOOL)isFlipped
{
    return YES;
}

@end

static void listbox_create_backing(Mel_NCtrl* ctrl)
{
    NSRect frame = NSMakeRect(
        (CGFloat)ctrl->pos.x,
        (CGFloat)ctrl->pos.y,
        (CGFloat)ctrl->size.x,
        (CGFloat)ctrl->size.y
    );

    NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:@"MelListboxColumn"];
    [column setWidth:frame.size.width];

    NSTableView* tableView = [[NSTableView alloc] initWithFrame:frame];
    [tableView addTableColumn:column];
    [tableView setHeaderView:nil];
    [tableView setAllowsMultipleSelection:NO];
    [tableView setUsesAlternatingRowBackgroundColors:YES];

    MelListboxDelegate* delegate = [[MelListboxDelegate alloc] init];
    delegate.listbox = (Mel_NListbox*)ctrl;
    [tableView setDataSource:delegate];
    [tableView setDelegate:delegate];

    NSScrollView* scrollView = [[NSScrollView alloc] initWithFrame:frame];
    [scrollView setDocumentView:tableView];
    [scrollView setHasVerticalScroller:YES];
    [scrollView setHasHorizontalScroller:NO];
    [scrollView setAutohidesScrollers:YES];
    [scrollView setBorderType:NSBezelBorder];

    objc_setAssociatedObject(scrollView, kListboxDelegateKey, delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(scrollView, kListboxTableViewKey, tableView, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    ctrl->backing = (__bridge_retained void*)scrollView;
}

static void listbox_destroy_backing(Mel_NCtrl* ctrl)
{
    if (!ctrl->backing)
        return;

    NSScrollView* scrollView = (__bridge_transfer NSScrollView*)ctrl->backing;
    NSTableView* tableView = objc_getAssociatedObject(scrollView, kListboxTableViewKey);
    [tableView setDataSource:nil];
    [tableView setDelegate:nil];
    objc_setAssociatedObject(scrollView, kListboxDelegateKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(scrollView, kListboxTableViewKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [scrollView removeFromSuperview];
    (void)scrollView;
    ctrl->backing = nullptr;
}

static void listbox_set_frame(Mel_NCtrl* ctrl, f32 x, f32 y, f32 w, f32 h)
{
    if (!ctrl->backing)
        return;

    NSScrollView* scrollView = (__bridge NSScrollView*)ctrl->backing;
    [scrollView setFrame:NSMakeRect((CGFloat)x, (CGFloat)y, (CGFloat)w, (CGFloat)h)];
}

static void listbox_set_visible(Mel_NCtrl* ctrl, bool visible)
{
    if (!ctrl->backing)
        return;

    NSScrollView* scrollView = (__bridge NSScrollView*)ctrl->backing;
    [scrollView setHidden:!visible];
}

static void listbox_set_enabled(Mel_NCtrl* ctrl, bool enabled)
{
    if (!ctrl->backing)
        return;

    NSScrollView* scrollView = (__bridge NSScrollView*)ctrl->backing;
    NSTableView* tableView = objc_getAssociatedObject(scrollView, kListboxTableViewKey);
    [tableView setEnabled:enabled];
}

static Mel_Vec2 listbox_preferred_size(Mel_NCtrl* ctrl)
{
    (void)ctrl;
    return mel_vec2(200.0f, 150.0f);
}

static void listbox_add_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static void listbox_remove_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static const Mel_NCtrl_VTable s_listbox_vtable = {
    .create_backing       = listbox_create_backing,
    .destroy_backing      = listbox_destroy_backing,
    .set_frame            = listbox_set_frame,
    .set_visible          = listbox_set_visible,
    .set_enabled          = listbox_set_enabled,
    .preferred_size       = listbox_preferred_size,
    .add_child_backing    = listbox_add_child_backing,
    .remove_child_backing = listbox_remove_child_backing,
};

const Mel_NCtrl_VTable* mel__nlistbox_vtable_osx(void)
{
    return &s_listbox_vtable;
}

void mel__nlistbox_set_items_platform(Mel_NListbox* listbox, const char** items, i32 count)
{
    assert(listbox != nullptr);
    (void)items;
    (void)count;
    if (!listbox->base.backing)
        return;

    NSScrollView* scrollView = (__bridge NSScrollView*)listbox->base.backing;
    NSTableView* tableView = objc_getAssociatedObject(scrollView, kListboxTableViewKey);
    [tableView reloadData];
}

void mel__nlistbox_set_selected_platform(Mel_NListbox* listbox, i32 index)
{
    assert(listbox != nullptr);
    if (!listbox->base.backing)
        return;

    NSScrollView* scrollView = (__bridge NSScrollView*)listbox->base.backing;
    NSTableView* tableView = objc_getAssociatedObject(scrollView, kListboxTableViewKey);

    if (index < 0) {
        [tableView deselectAll:nil];
    } else {
        NSIndexSet* indexSet = [NSIndexSet indexSetWithIndex:(NSUInteger)index];
        [tableView selectRowIndexes:indexSet byExtendingSelection:NO];
    }
}
