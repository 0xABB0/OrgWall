#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "../ui.native.tableview.h"
#include "../string.str8.h"

static const void* kTableViewDelegateKey = &kTableViewDelegateKey;
static const void* kTableViewInnerKey = &kTableViewInnerKey;

@interface MelTableViewDelegate : NSObject <NSTableViewDataSource, NSTableViewDelegate>
@property (nonatomic, assign) Mel_NTableView* table;
@end

@implementation MelTableViewDelegate

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    (void)tableView;
    if (!self.table)
        return 0;
    return (NSInteger)self.table->row_count;
}

- (id)tableView:(NSTableView*)tableView objectValueForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row
{
    (void)tableView;
    if (!self.table || !self.table->data_cb)
        return @"";

    NSString* identifier = [tableColumn identifier];
    i32 col_index = (i32)[identifier integerValue];

    if (col_index < 0 || col_index >= self.table->column_count)
        return @"";

    str8 value = self.table->data_cb((i32)row, col_index, self.table->user_data);
    if (str8_is_empty(value))
        return @"";

    char buf[1024];
    str8_to_buf(value, buf, sizeof(buf));
    return [NSString stringWithUTF8String:buf];
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification
{
    NSTableView* tv = [notification object];
    if (!self.table)
        return;

    NSInteger row = [tv selectedRow];
    self.table->selected = (i32)row;

    if (self.table->on_select)
        self.table->on_select(self.table->selected, self.table->user_data);
}

@end

static void tableview_create_backing(Mel_NCtrl* ctrl)
{
    Mel_NTableView* table = (Mel_NTableView*)ctrl;

    NSRect frame = NSMakeRect(
        (CGFloat)ctrl->pos.x,
        (CGFloat)ctrl->pos.y,
        (CGFloat)ctrl->size.x,
        (CGFloat)ctrl->size.y
    );

    NSTableView* tableView = [[NSTableView alloc] initWithFrame:frame];
    [tableView setAllowsMultipleSelection:NO];
    [tableView setUsesAlternatingRowBackgroundColors:YES];

    for (i32 i = 0; i < table->column_count; i++) {
        NSString* identifier = [NSString stringWithFormat:@"%d", (int)i];
        NSTableColumn* col = [[NSTableColumn alloc] initWithIdentifier:identifier];

        if (!str8_is_empty(table->columns[i].title)) {
            char buf[1024];
            str8_to_buf(table->columns[i].title, buf, sizeof(buf));
            [[col headerCell] setStringValue:[NSString stringWithUTF8String:buf]];
        }

        if (table->columns[i].width > 0)
            [col setWidth:(CGFloat)table->columns[i].width];

        [tableView addTableColumn:col];
    }

    MelTableViewDelegate* delegate = [[MelTableViewDelegate alloc] init];
    delegate.table = table;
    [tableView setDataSource:delegate];
    [tableView setDelegate:delegate];

    NSScrollView* scrollView = [[NSScrollView alloc] initWithFrame:frame];
    [scrollView setDocumentView:tableView];
    [scrollView setHasVerticalScroller:YES];
    [scrollView setHasHorizontalScroller:YES];
    [scrollView setAutohidesScrollers:YES];
    [scrollView setBorderType:NSBezelBorder];

    objc_setAssociatedObject(scrollView, kTableViewDelegateKey, delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(scrollView, kTableViewInnerKey, tableView, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    ctrl->backing = (__bridge_retained void*)scrollView;
}

static void tableview_destroy_backing(Mel_NCtrl* ctrl)
{
    if (!ctrl->backing)
        return;

    NSScrollView* scrollView = (__bridge_transfer NSScrollView*)ctrl->backing;
    NSTableView* tableView = objc_getAssociatedObject(scrollView, kTableViewInnerKey);
    [tableView setDataSource:nil];
    [tableView setDelegate:nil];
    objc_setAssociatedObject(scrollView, kTableViewDelegateKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(scrollView, kTableViewInnerKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [scrollView removeFromSuperview];
    (void)scrollView;
    ctrl->backing = nullptr;
}

static void tableview_set_frame(Mel_NCtrl* ctrl, f32 x, f32 y, f32 w, f32 h)
{
    if (!ctrl->backing)
        return;

    NSScrollView* scrollView = (__bridge NSScrollView*)ctrl->backing;
    [scrollView setFrame:NSMakeRect((CGFloat)x, (CGFloat)y, (CGFloat)w, (CGFloat)h)];
}

static void tableview_set_visible(Mel_NCtrl* ctrl, bool visible)
{
    if (!ctrl->backing)
        return;

    NSScrollView* scrollView = (__bridge NSScrollView*)ctrl->backing;
    [scrollView setHidden:!visible];
}

static void tableview_set_enabled(Mel_NCtrl* ctrl, bool enabled)
{
    if (!ctrl->backing)
        return;

    NSScrollView* scrollView = (__bridge NSScrollView*)ctrl->backing;
    NSTableView* tableView = objc_getAssociatedObject(scrollView, kTableViewInnerKey);
    [tableView setEnabled:enabled];
}

static Mel_Vec2 tableview_preferred_size(Mel_NCtrl* ctrl)
{
    (void)ctrl;
    return mel_vec2(400.0f, 200.0f);
}

static void tableview_add_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static void tableview_remove_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static const Mel_NCtrl_VTable s_tableview_vtable = {
    .create_backing       = tableview_create_backing,
    .destroy_backing      = tableview_destroy_backing,
    .set_frame            = tableview_set_frame,
    .set_visible          = tableview_set_visible,
    .set_enabled          = tableview_set_enabled,
    .preferred_size       = tableview_preferred_size,
    .add_child_backing    = tableview_add_child_backing,
    .remove_child_backing = tableview_remove_child_backing,
};

const Mel_NCtrl_VTable* mel__ntableview_vtable(void)
{
    return &s_tableview_vtable;
}

void mel__ntableview_reload_platform(Mel_NTableView* table)
{
    assert(table != nullptr);
    if (!table->base.backing)
        return;

    NSScrollView* scrollView = (__bridge NSScrollView*)table->base.backing;
    NSTableView* tableView = objc_getAssociatedObject(scrollView, kTableViewInnerKey);
    [tableView reloadData];
}

void mel__ntableview_set_selected_platform(Mel_NTableView* table, i32 row)
{
    assert(table != nullptr);
    if (!table->base.backing)
        return;

    NSScrollView* scrollView = (__bridge NSScrollView*)table->base.backing;
    NSTableView* tableView = objc_getAssociatedObject(scrollView, kTableViewInnerKey);

    if (row < 0) {
        [tableView deselectAll:nil];
    } else {
        NSIndexSet* indexSet = [NSIndexSet indexSetWithIndex:(NSUInteger)row];
        [tableView selectRowIndexes:indexSet byExtendingSelection:NO];
    }
}
