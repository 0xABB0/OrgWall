#include "macos.h"

@implementation MelGuiLabel
@end

void mel_gui__backend_label_create(Mel_Gui_Widget* w, str8 text)
{
    @autoreleasepool {
        MelGuiLabel* label = [[MelGuiLabel alloc] initWithFrame:NSMakeRect(0, 0, w->width, w->height)];
        label.handle           = w->self;
        label.editable         = NO;
        label.selectable       = NO;
        label.bezeled          = NO;
        label.drawsBackground  = NO;
        label.bordered         = NO;
        label.stringValue      = mel_gui__macos_nsstring(text);
        label.alignment        = NSTextAlignmentLeft;
        label.lineBreakMode    = NSLineBreakByWordWrapping;
        label.cell.wraps       = YES;
        label.cell.scrollable  = NO;

        mel_gui__macos_install_child(w, label);
    }
}
