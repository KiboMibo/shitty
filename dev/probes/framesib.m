// Пережил ли сосед contentView в фрейм-вью смену styleMask и полноэкранный режим.
#import <Cocoa/Cocoa.h>

@interface Probe: NSView @end
@implementation Probe
- (NSView*)hitTest:(NSPoint)p { (void)p; return nil; }
@end

static const char* present(NSWindow* w, NSView* probe) {
    NSView* frameView = w.contentView.superview;
    if (frameView == nil) return "NO FRAME VIEW";
    return [frameView.subviews containsObject:probe] ? "present" : "GONE";
}

int main(void) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        NSWindowStyleMask mask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                 NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        NSWindow* w = [[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,400,300)
                        styleMask:mask backing:NSBackingStoreBuffered defer:NO];
        NSView* content = [[NSView alloc] initWithFrame:NSMakeRect(0,0,400,300)];
        w.contentView = content;
        NSView* frameView = content.superview;
        printf("frame view class: %s\n", frameView ? [NSStringFromClass([frameView class]) UTF8String] : "(nil)");
        Probe* probe = [[Probe alloc] initWithFrame:frameView.bounds];
        probe.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [frameView addSubview:probe positioned:NSWindowBelow relativeTo:content];
        printf("after insert                        : %s\n", present(w, probe));

        w.styleMask &= ~NSWindowStyleMaskMiniaturizable;              // platform_cocoa.mm:1365
        printf("after -Miniaturizable (quick window): %s\n", present(w, probe));

        w.styleMask |= NSWindowStyleMaskFullSizeContentView;          // ui_csd_tabs.mm:359
        printf("after +FullSizeContentView (autoHide): %s\n", present(w, probe));

        w.styleMask &= ~NSWindowStyleMaskFullSizeContentView;         // ui_csd_tabs.mm:362
        printf("after -FullSizeContentView          : %s\n", present(w, probe));

        printf("contentView identity unchanged      : %s\n", w.contentView == content ? "yes" : "NO");
        printf("hitTest at many points              : ");
        int hits = 0;
        for (int x = 0; x < 400; x += 7)
            for (int y = 0; y < 300; y += 7)
                if ([probe hitTest:NSMakePoint(x, y)] != nil) hits++;
        // и за пределами прямоугольника тоже
        for (int x = -50; x < 460; x += 11)
            for (int y = -50; y < 360; y += 11)
                if ([probe hitTest:NSMakePoint(x, y)] != nil) hits++;
        printf("%d hits (0 = недостижима)\n", hits);
        printf("window hitTest never returns probe  : ");
        int leaked = 0;
        for (int x = 0; x < 400; x += 13)
            for (int y = 0; y < 300; y += 13) {
                NSView* hit = [w.contentView.superview hitTest:NSMakePoint(x, y)];
                if (hit == probe) leaked++;
            }
        printf("%d leaks\n", leaked);
    }
    return 0;
}
