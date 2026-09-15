// T9 probe: macOS 27 glass - effectIsInteractive and NSView.cornerConfiguration.
//
// Capture discipline follows CLAUDE.md, paid for by T3/T4/T6/T7:
//   - the backdrop is MY OWN full-screen opaque window at level 0, directly
//     below the probe window (level -1 does not composite through glass; T7);
//   - the capture rect is the probe window's rect AS THE WINDOW SERVER
//     REPORTS IT, re-read before every shot, never cached (T6);
//   - before every shot the whole on-screen window stack is walked, and if
//     anything that is not mine sits above my backdrop and intersects the
//     capture rect, THE SHOT IS NOT TAKEN. Mechanical, not advisory (T7);
//   - screencapture -R silently clips at the screen edge, so the requested
//     rect is written next to every frame and checked afterwards.
//
// Nothing here is ever pointed at a product window.

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Mirrors the pair ext/plt/platform_cocoa.mm keeps around newer AppKit.
#if defined(MAC_OS_VERSION_27_0) && MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_VERSION_27_0
#define T9_SDK_27 1
#else
#define T9_SDK_27 0
#endif

// ---------------------------------------------------------------- geometry

static const CGFloat kProbeW = 900;
static const CGFloat kProbeH = 560;
// Sidebar metrics copied from lib/shitty/ui_sidebar_tabs.mm so the pill in
// this probe is the product's pill and not a lookalike.
static const CGFloat kSidebarW = 220;
static const CGFloat kRowH = 46;
static const CGFloat kListTop = 6;
static const CGFloat kPillInset = 6;
static const CGFloat kPillRadius = 6;
static const CGFloat kWindowRadius = 12;   // -quickCornerRadius default

// ------------------------------------------------------------ the backdrop

// A synthetic field: one flat colour plus a black hairline every 8 points.
// The flat part is what a mean is taken over; the hairlines prove a blur
// happened - they survive unblurred and vanish blurred (T3's rowvar).
@interface T9FieldView: NSView {
@public
    CGFloat level;
}
@end

@implementation T9FieldView
- (BOOL)isOpaque { return YES; }
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    const NSRect b = self.bounds;
    [[NSColor colorWithSRGBRed:level green:level blue:level alpha:1.0] setFill];
    NSRectFill(b);
    [[NSColor colorWithSRGBRed:0 green:0 blue:0 alpha:1.0] setFill];
    for (CGFloat y = NSMinY(b); y < NSMaxY(b); y += 8) {
        NSRectFill(NSMakeRect(NSMinX(b), y, b.size.width, 1));
    }
}
@end

// Counts mouse-downs the panel actually handed on to the pill: a forwarded
// event that never left the panel would look exactly like glass ignoring it.
static int gForwarded = 0;

// ------------------------------------------- corner-configuration subclasses

#if T9_SDK_27

// A plain NSView that declares a corner configuration and, unless told to,
// does NOT apply it. If the system both reads the getter and shapes the view,
// this comes out rounded with no other help.
API_AVAILABLE(macos(27.0))
@interface T9CornerView: NSView {
@public
    NSViewCornerConfiguration* wanted;
    int getterCalls;
    int didChangeCalls;
    BOOL applyToLayer;
}
@end

@implementation T9CornerView
- (NSViewCornerConfiguration*)cornerConfiguration {
    getterCalls += 1;
    return wanted;
}
- (void)viewDidChangeEffectiveCornerRadii {
    [super viewDidChangeEffectiveCornerRadii];
    didChangeCalls += 1;
    if (applyToLayer) {
        NSViewCornerRadii* const r = self.effectiveCornerRadii;
        self.wantsLayer = YES;
        self.layer.cornerRadius = r == nil ? 0 : r.topLeft;
        self.layer.masksToBounds = r != nil;
    }
}
- (BOOL)isOpaque { return NO; }
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    [[NSColor colorWithSRGBRed:1.0 green:0.35 blue:0.0 alpha:1.0] setFill];
    NSRectFill(self.bounds);
}
@end

// The same declaration on glass. This is the one the task needs: does
// NSGlassEffectView shape its own glass from cornerConfiguration, and does
// that co-exist with the cornerRadius property we set today?
API_AVAILABLE(macos(27.0))
@interface T9GlassCornerView: NSGlassEffectView {
@public
    NSViewCornerConfiguration* wanted;
    int getterCalls;
    int didChangeCalls;
}
@end

@implementation T9GlassCornerView
- (NSViewCornerConfiguration*)cornerConfiguration {
    getterCalls += 1;
    return wanted;
}
- (void)viewDidChangeEffectiveCornerRadii {
    [super viewDidChangeEffectiveCornerRadii];
    didChangeCalls += 1;
}
- (NSView*)hitTest:(NSPoint)p { (void)p; return nil; }
@end

// A container with a known rounded shape, so a child asking for
// containerConcentric has something to be concentric with.
API_AVAILABLE(macos(27.0))
@interface T9ContainerView: NSView {
@public
    CGFloat radius;
}
@end

@implementation T9ContainerView
- (NSViewCornerConfiguration*)cornerConfiguration {
    return [NSViewCornerConfiguration configurationWithRadius:[NSViewCornerRadius fixedRadius:radius]];
}
- (void)viewDidChangeEffectiveCornerRadii {
    [super viewDidChangeEffectiveCornerRadii];
    NSViewCornerRadii* const r = self.effectiveCornerRadii;
    self.wantsLayer = YES;
    self.layer.cornerRadius = r == nil ? 0 : r.topLeft;
    self.layer.masksToBounds = YES;
}
- (BOOL)isOpaque { return NO; }
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    [[NSColor colorWithSRGBRed:0.15 green:0.55 blue:0.95 alpha:1.0] setFill];
    NSRectFill(self.bounds);
}
@end

#endif  // T9_SDK_27

// ------------------------------------------------------ probe window content

// The frosted backdrop -backgroundBlur blur asks for, in the same place the
// glass one goes. Nothing in this task touches the product's own
// PltBackdropView; this exists so "unchanged" is a measurement here too.
@interface T9BlurView: NSVisualEffectView
@end
@implementation T9BlurView
- (NSView*)hitTest:(NSPoint)p { (void)p; return nil; }
@end

@interface T9RootView: NSView
@end
@implementation T9RootView
- (BOOL)isOpaque { return NO; }
@end

// Stands in for the terminal's CAMetalLayer: a translucent flat fill over the
// whole window, which is what the glass backdrop has above it in the product.
@interface T9TerminalView: NSView {
@public
    CGFloat alpha;
}
@end
@implementation T9TerminalView
- (BOOL)isOpaque { return NO; }
- (NSView*)hitTest:(NSPoint)p { (void)p; return nil; }
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    [[NSColor colorWithSRGBRed:0.07 green:0.08 blue:0.11 alpha:alpha] setFill];
    NSRectFill(self.bounds);
}
@end

// The sidebar strip: text and a hairline only, exactly as T5 left it when the
// window carries glass. Flipped, like the product's panel.
@interface T9SidebarView: NSView {
@public
    BOOL eatsClicks;
    // When set, this panel keeps hit testing (as the product's does) and hands
    // the mouse-down on to the pill underneath it by hand. The question that
    // settles: does interactive glass respond to an event it is SENT, or only
    // to one hit testing gave it?
    NSView* forwardTo;
}
@end
@implementation T9SidebarView
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return NO; }
- (NSView*)hitTest:(NSPoint)p {
    if (eatsClicks) { return [super hitTest:p]; }
    return nil;
}
- (void)mouseDown:(NSEvent*)e {
    if (forwardTo != nil) { gForwarded += 1; [forwardTo mouseDown:e]; return; }
    [super mouseDown:e];
}
- (void)mouseUp:(NSEvent*)e {
    if (forwardTo != nil) { [forwardTo mouseUp:e]; return; }
    [super mouseUp:e];
}
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    const NSRect b = self.bounds;
    NSDictionary* const attrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:12],
        NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.92 green:0.93 blue:0.95 alpha:1.0],
    };
    NSDictionary* const dim = @{
        NSFontAttributeName: [NSFont systemFontOfSize:10],
        NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:0.92 green:0.93 blue:0.95 alpha:0.55],
    };
    for (int i = 0; i < 6; i += 1) {
        const CGFloat y = kListTop + kRowH * (CGFloat)(i);
        if (y + kRowH > NSMaxY(b)) { break; }
        [@"~/Projects/shitty" drawAtPoint:NSMakePoint(NSMinX(b) + 14, y + 7) withAttributes:attrs];
        [@"zsh - main" drawAtPoint:NSMakePoint(NSMinX(b) + 14, y + 25) withAttributes:dim];
    }
    [[NSColor colorWithSRGBRed:0 green:0 blue:0 alpha:0.70] setFill];
    NSRectFill(NSMakeRect(NSMaxX(b) - 1, NSMinY(b), 1, b.size.height));
}
@end

// The pill rect of row `at`, in the panel's flipped coordinates. Same shape
// as sidebarPillFor() in the product, same numbers.
static NSRect t9PillFor(NSRect bounds, int at) {
    const NSRect row = NSMakeRect(NSMinX(bounds), NSMinY(bounds) + kListTop + kRowH * (CGFloat)(at), bounds.size.width, kRowH);
    return NSInsetRect(row, kPillInset, 2);
}

// ------------------------------------------------------------ stack and shots

static FILE* gLog = NULL;

// T5 paid for this: a frame taken after a posted event proves nothing about
// behaviour unless the event is independently known to have ARRIVED. A local
// monitor sees what the app is handed regardless of which view hit testing
// then gives it to, which an -mouseDown: override would not.
static int gDown = 0, gUp = 0, gMoved = 0, gEntered = 0;

static BOOL t9WindowRect(CGWindowID wid, CGRect* out) {
    const CGWindowID ids[1] = { wid };
    CFArrayRef const arr = CFArrayCreate(NULL, (const void**)ids, 1, NULL);
    CFArrayRef const info = CGWindowListCreateDescriptionFromArray(arr);
    CFRelease(arr);
    if (info == NULL) { return NO; }
    BOOL ok = NO;
    if (CFArrayGetCount(info) == 1) {
        CFDictionaryRef const d = (CFDictionaryRef)CFArrayGetValueAtIndex(info, 0);
        CFDictionaryRef const b = (CFDictionaryRef)CFDictionaryGetValue(d, kCGWindowBounds);
        if (b != NULL && CGRectMakeWithDictionaryRepresentation(b, out)) { ok = YES; }
    }
    CFRelease(info);
    return ok;
}

// Walks the on-screen stack front to back and refuses the shot if anything
// that is not mine sits above my backdrop and touches the capture rect.
//
// This is the form T7 paid for: "what occludes me" is only half the question,
// because glass samples what is BEHIND. Everything from the front of the list
// down to my own backdrop is inspected, not only what is in front of my window.
static BOOL t9StackIsClean(CGWindowID mine, CGWindowID backdrop, CGRect capture) {
    CFArrayRef const list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, kCGNullWindowID);
    if (list == NULL) { fprintf(gLog, "STACK: no list\n"); return NO; }
    const CFIndex n = CFArrayGetCount(list);
    BOOL seenBackdrop = NO;
    BOOL clean = YES;
    int foreign = 0;
    fprintf(gLog, "STACK: %ld on-screen windows, front to back; capture %.0fx%.0f@%.0f,%.0f\n",
            (long)(n), capture.size.width, capture.size.height, capture.origin.x, capture.origin.y);
    for (CFIndex i = 0; i < n; i += 1) {
        CFDictionaryRef const d = (CFDictionaryRef)CFArrayGetValueAtIndex(list, i);
        CFNumberRef const num = (CFNumberRef)CFDictionaryGetValue(d, kCGWindowNumber);
        int wid = 0;
        if (num != NULL) { CFNumberGetValue(num, kCFNumberIntType, &wid); }
        CFStringRef const owner = (CFStringRef)CFDictionaryGetValue(d, kCGWindowOwnerName);
        CFStringRef const name = (CFStringRef)CFDictionaryGetValue(d, kCGWindowName);
        CFDictionaryRef const bd = (CFDictionaryRef)CFDictionaryGetValue(d, kCGWindowBounds);
        CGRect r = CGRectZero;
        if (bd != NULL) { CGRectMakeWithDictionaryRepresentation(bd, &r); }
        const BOOL hits = CGRectIntersectsRect(r, capture);
        const char* ownerc = owner == NULL ? "?" : [(NSString*)owner UTF8String];
        const char* namec = name == NULL ? "" : [(NSString*)name UTF8String];
        const BOOL isMine = (wid == (int)(mine)) || (wid == (int)(backdrop));
        // The pointer is its own window and screencapture -x does not draw it.
        // Named here so the exclusion is auditable rather than implicit.
        const BOOL isCursor = owner != NULL && [(NSString*)owner isEqualToString:@"Window Server"]
                              && name != NULL && [(NSString*)name isEqualToString:@"Cursor"];
        const char* verdict = "below-backdrop";
        if (!seenBackdrop) {
            if (isMine) { verdict = "MINE"; }
            else if (!hits) { verdict = "misses-rect"; }
            else if (isCursor) { verdict = "cursor(not-drawn)"; }
            else { verdict = "FOREIGN-IN-RECT"; clean = NO; foreign += 1; }
        }
        fprintf(gLog, "  [%2ld] wid=%-7d %-26.26s %-26.26s %5.0fx%-5.0f@%5.0f,%-5.0f  %s\n",
                (long)(i), wid, ownerc, namec, r.size.width, r.size.height, r.origin.x, r.origin.y, verdict);
        if (wid == (int)(backdrop)) { seenBackdrop = YES; }
    }
    CFRelease(list);
    if (!seenBackdrop) { fprintf(gLog, "STACK: my own backdrop is not on screen\n"); clean = NO; }
    fprintf(gLog, "STACK: foreign-in-rect=%d -> %s\n", foreign, clean ? "SHOOT" : "REFUSE");
    return clean;
}

// A real AppKit pump, not -[NSRunLoop runUntilDate:]. The run loop alone
// leaves posted mouse events sitting in the application's own queue: the
// first version of this probe measured a byte-identical YES/NO difference
// that way, with down=0 up=0 moved=0 on the witness - the null result was the
// harness, not the API.
static void t9Settle(double seconds) {
    NSDate* const until = [NSDate dateWithTimeIntervalSinceNow:seconds];
    for (;;) {
        NSEvent* const e = [NSApp nextEventMatchingMask:NSEventMaskAny
                                              untilDate:until
                                                 inMode:NSDefaultRunLoopMode
                                                dequeue:YES];
        if (e == nil) { break; }
        [NSApp sendEvent:e];
    }
}

typedef struct {
    NSWindow* backWin;
    NSWindow* probeWin;
    const char* outdir;
    int shots;
    int refused;
} T9Ctx;

static BOOL t9Shoot(T9Ctx* ctx, const char* name) {
    t9Settle(0.40);
    CGRect r;
    if (!t9WindowRect((CGWindowID)([ctx->probeWin windowNumber]), &r)) {
        fprintf(gLog, "SHOT %s: no rect from the window server\n", name);
        ctx->refused += 1;
        return NO;
    }
    {
        const NSPoint where = [NSEvent mouseLocation];
        fprintf(gLog, "SHOT %s  events so far: down=%d up=%d moved=%d entered=%d forwarded=%d; pointer at %.0f,%.0f\n",
                name, gDown, gUp, gMoved, gEntered, gForwarded, where.x, where.y);
    }
    if (!t9StackIsClean((CGWindowID)([ctx->probeWin windowNumber]),
                        (CGWindowID)([ctx->backWin windowNumber]), r)) {
        fprintf(gLog, "SHOT %s: REFUSED, no file written\n", name);
        ctx->refused += 1;
        return NO;
    }
    NSString* const path = [NSString stringWithFormat:@"%s/%s.png", ctx->outdir, name];
    NSTask* const t = [[NSTask alloc] init];
    t.launchPath = @"/usr/sbin/screencapture";
    t.arguments = @[@"-x", @"-o", @"-t", @"png",
                    [NSString stringWithFormat:@"-R%.0f,%.0f,%.0f,%.0f", r.origin.x, r.origin.y, r.size.width, r.size.height],
                    path];
    [t launch];
    [t waitUntilExit];
    const int rc = t.terminationStatus;
    [t release];
    fprintf(gLog, "SHOT %s: rc=%d requested %.0fx%.0f at %.0f,%.0f -> %s\n",
            name, rc, r.size.width, r.size.height, r.origin.x, r.origin.y, [path UTF8String]);
    // The requested rect goes next to the frame: screencapture -R clips at the
    // screen edge and still returns 0, so the checker needs the request.
    NSString* const side = [NSString stringWithFormat:@"%s/%s.rect", ctx->outdir, name];
    [[NSString stringWithFormat:@"%.0f %.0f %.0f %.0f %.2f\n", r.origin.x, r.origin.y, r.size.width, r.size.height,
      ctx->probeWin.backingScaleFactor] writeToFile:side atomically:YES encoding:NSUTF8StringEncoding error:NULL];
    ctx->shots += 1;
    return rc == 0;
}

// ------------------------------------------------------------------- events

static void t9Move(CGPoint p) {
    CGEventRef const e = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, p, kCGMouseButtonLeft);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}
static void t9Down(CGPoint p) {
    CGEventRef const e = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseDown, p, kCGMouseButtonLeft);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}
static void t9Up(CGPoint p) {
    CGEventRef const e = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseUp, p, kCGMouseButtonLeft);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}

// ------------------------------------------------------------------- scenes

static const char* argStr(int argc, const char** argv, const char* key, const char* fallback) {
    for (int i = 1; i + 1 < argc; i += 1) {
        if (strcmp(argv[i], key) == 0) { return argv[i + 1]; }
    }
    return fallback;
}
static double argNum(int argc, const char** argv, const char* key, double fallback) {
    const char* const s = argStr(argc, argv, key, NULL);
    return s == NULL ? fallback : atof(s);
}

int main(int argc, const char** argv) {
    @autoreleasepool {
        const char* const outdir = argStr(argc, argv, "--out", ".");
        const char* const scene = argStr(argc, argv, "--scene", "pill");
        const double field = argNum(argc, argv, "--field", 0.10);
        const int interactive = (int)(argNum(argc, argv, "--interactive", 0));
        const int pillOnTop = (int)(argNum(argc, argv, "--pill-on-top", 0));
        const int noPill = (int)(argNum(argc, argv, "--no-pill", 0));
        const int forward = (int)(argNum(argc, argv, "--forward", 0));
        const int newCorners = (int)(argNum(argc, argv, "--new-corners", 0));
        const char* const tag = argStr(argc, argv, "--tag", "frame");
        const char* const backdropKind = argStr(argc, argv, "--backdrop", "glass");

        NSString* const logPath = [NSString stringWithFormat:@"%s/%s.log", outdir, tag];
        gLog = fopen([logPath UTF8String], "w");
        if (gLog == NULL) { gLog = stderr; }
        setvbuf(gLog, NULL, _IOLBF, 0);
        fprintf(gLog, "T9 probe: scene=%s field=%.2f interactive=%d pillOnTop=%d noPill=%d newCorners=%d forward=%d SDK27=%d\n",
                scene, field, interactive, pillOnTop, noPill, newCorners, forward, T9_SDK_27);

        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        [NSEvent addLocalMonitorForEventsMatchingMask:
            (NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp | NSEventMaskMouseMoved | NSEventMaskMouseEntered | NSEventMaskMouseExited)
                                              handler:^NSEvent*(NSEvent* e) {
            switch (e.type) {
                case NSEventTypeLeftMouseDown: gDown += 1; break;
                case NSEventTypeLeftMouseUp: gUp += 1; break;
                case NSEventTypeMouseMoved: gMoved += 1; break;
                case NSEventTypeMouseEntered: gEntered += 1; break;
                default: break;
            }
            return e;
        }];

        NSScreen* const screen = [NSScreen screens][0];
        const NSRect sf = screen.frame;

        // ---- my own backdrop: full screen, opaque, ordinary level 0.
        // Level -1 does not composite through glass (T7, measured); level 0
        // does. Nothing of the user's screen survives inside its rect.
        NSWindow* const backWin = [[NSWindow alloc] initWithContentRect:sf
                                                             styleMask:NSWindowStyleMaskBorderless
                                                               backing:NSBackingStoreBuffered
                                                                 defer:NO];
        backWin.opaque = YES;
        backWin.hasShadow = NO;
        backWin.level = NSNormalWindowLevel;
        backWin.ignoresMouseEvents = YES;
        backWin.collectionBehavior = NSWindowCollectionBehaviorStationary | NSWindowCollectionBehaviorIgnoresCycle;
        T9FieldView* const fieldView = [[T9FieldView alloc] initWithFrame:NSMakeRect(0, 0, sf.size.width, sf.size.height)];
        fieldView->level = field;
        fieldView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        backWin.contentView = fieldView;
        [backWin setFrame:sf display:YES];
        [backWin orderFrontRegardless];

        // ---- the probe window, centred, well clear of every screen edge so
        // screencapture -R has nothing to clip silently.
        const NSRect pf = NSMakeRect(NSMidX(sf) - kProbeW / 2, NSMidY(sf) - kProbeH / 2, kProbeW, kProbeH);
        NSWindow* const probeWin = [[NSWindow alloc] initWithContentRect:pf
                                                              styleMask:NSWindowStyleMaskBorderless
                                                                backing:NSBackingStoreBuffered
                                                                  defer:NO];
        probeWin.opaque = NO;
        probeWin.backgroundColor = [NSColor clearColor];
        probeWin.hasShadow = NO;
        probeWin.level = NSNormalWindowLevel;
        probeWin.acceptsMouseMovedEvents = YES;
        T9RootView* const root = [[T9RootView alloc] initWithFrame:NSMakeRect(0, 0, kProbeW, kProbeH)];
        probeWin.contentView = root;

        T9Ctx ctx;
        ctx.backWin = backWin;
        ctx.probeWin = probeWin;
        ctx.outdir = outdir;
        ctx.shots = 0;
        ctx.refused = 0;

        NSMutableArray* const pillRects = [NSMutableArray array];

        if (strcmp(scene, "corners") == 0) {
#if T9_SDK_27
            if (@available(macOS 27.0, *)) {
                // Six tiles, 260x84, on a 3x2 grid. Each one asks a different
                // question of cornerConfiguration; the numbers are printed and
                // the shapes are in the frame.
                const CGFloat tw = 260, th = 84;
                const CGFloat x0 = 30, y0 = kProbeH - 30 - th, dx = 290, dy = 110;

                // (1) plain view, capsule, NOT applied by us.
                T9CornerView* const a = [[T9CornerView alloc] initWithFrame:NSMakeRect(x0, y0, tw, th)];
                a->wanted = [[NSViewCornerConfiguration capsuleCornerConfiguration] retain];
                a->applyToLayer = NO;
                [root addSubview:a];

                // (2) plain view, capsule, applied the way the header asks.
                T9CornerView* const b = [[T9CornerView alloc] initWithFrame:NSMakeRect(x0 + dx, y0, tw, th)];
                b->wanted = [[NSViewCornerConfiguration capsuleCornerConfiguration] retain];
                b->applyToLayer = YES;
                [root addSubview:b];

                // (3) plain view, fixed 24, applied.
                T9CornerView* const c = [[T9CornerView alloc] initWithFrame:NSMakeRect(x0 + 2 * dx, y0, tw, th)];
                c->wanted = [[NSViewCornerConfiguration configurationWithRadius:[NSViewCornerRadius fixedRadius:24]] retain];
                c->applyToLayer = YES;
                [root addSubview:c];

                // (4) glass, capsule, nothing else set.
                T9GlassCornerView* const g1 = [[T9GlassCornerView alloc] initWithFrame:NSMakeRect(x0, y0 - dy, tw, th)];
                g1->wanted = [[NSViewCornerConfiguration capsuleCornerConfiguration] retain];
                g1.style = NSGlassEffectViewStyleClear;
                g1.translatesAutoresizingMaskIntoConstraints = YES;
                [root addSubview:g1];

                // (5) glass, capsule, and cornerRadius ALSO set to 6 - the
                // conflict the task asks about.
                T9GlassCornerView* const g2 = [[T9GlassCornerView alloc] initWithFrame:NSMakeRect(x0 + dx, y0 - dy, tw, th)];
                g2->wanted = [[NSViewCornerConfiguration capsuleCornerConfiguration] retain];
                g2.style = NSGlassEffectViewStyleClear;
                g2.cornerRadius = kPillRadius;
                g2.translatesAutoresizingMaskIntoConstraints = YES;
                [root addSubview:g2];

                // (6) control: glass, no configuration, cornerRadius 6 - today.
                NSGlassEffectView* const g3 = [[NSGlassEffectView alloc] initWithFrame:NSMakeRect(x0 + 2 * dx, y0 - dy, tw, th)];
                g3.style = NSGlassEffectViewStyleClear;
                g3.cornerRadius = kPillRadius;
                g3.translatesAutoresizingMaskIntoConstraints = YES;
                [root addSubview:g3];

                // (7) concentric: a rounded container with a glass child that
                // asks for containerConcentric with a minimum.
                T9ContainerView* const box = [[T9ContainerView alloc] initWithFrame:NSMakeRect(x0, y0 - dy - 30 - 144, tw + dx, 144)];
                box->radius = 28;
                [root addSubview:box];
                T9GlassCornerView* const g4 = [[T9GlassCornerView alloc] initWithFrame:NSInsetRect(box.bounds, 14, 14)];
                g4->wanted = [[NSViewCornerConfiguration configurationWithRadius:[NSViewCornerRadius containerConcentricRadiusWithMinimum:kPillRadius]] retain];
                g4.style = NSGlassEffectViewStyleClear;
                g4.translatesAutoresizingMaskIntoConstraints = YES;
                [box addSubview:g4];

                // (8) the same concentric child in a square container: the
                // minimum is what it must fall back to.
                T9ContainerView* const box2 = [[T9ContainerView alloc] initWithFrame:NSMakeRect(x0 + 2 * dx, y0 - dy - 30 - 144, tw, 144)];
                box2->radius = 0;
                [root addSubview:box2];
                T9GlassCornerView* const g5 = [[T9GlassCornerView alloc] initWithFrame:NSInsetRect(box2.bounds, 14, 14)];
                g5->wanted = [[NSViewCornerConfiguration configurationWithRadius:[NSViewCornerRadius containerConcentricRadiusWithMinimum:kPillRadius]] retain];
                g5.style = NSGlassEffectViewStyleClear;
                g5.translatesAutoresizingMaskIntoConstraints = YES;
                [box2 addSubview:g5];

                [probeWin makeKeyAndOrderFront:nil];
                [NSApp activateIgnoringOtherApps:YES];
                t9Settle(0.8);

                struct { const char* name; NSView* v; } rows[] = {
                    { "plain capsule, not applied",        a },
                    { "plain capsule, applied",            b },
                    { "plain fixed 24, applied",           c },
                    { "glass capsule",                     g1 },
                    { "glass capsule + cornerRadius 6",    g2 },
                    { "glass cornerRadius 6 (control)",    g3 },
                    { "container fixed 28",                box },
                    { "glass concentric(min 6) in r=28",   g4 },
                    { "container fixed 0",                 box2 },
                    { "glass concentric(min 6) in r=0",    g5 },
                };
                for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i += 1) {
                    NSView* const v = rows[i].v;
                    NSViewCornerRadii* const r = v.effectiveCornerRadii;
                    NSViewCornerConfiguration* const cc = v.cornerConfiguration;
                    fprintf(gLog, "RADII %-36s size=%.0fx%.0f config=%s effective=%s\n",
                            rows[i].name, v.frame.size.width, v.frame.size.height,
                            cc == nil ? "nil" : "set",
                            r == nil ? "nil" : [[NSString stringWithFormat:@"tl=%.2f tr=%.2f bl=%.2f br=%.2f", r.topLeft, r.topRight, r.bottomLeft, r.bottomRight] UTF8String]);
                }
                fprintf(gLog, "CALLS plain-not-applied getter=%d didChange=%d\n", a->getterCalls, a->didChangeCalls);
                fprintf(gLog, "CALLS plain-applied     getter=%d didChange=%d\n", b->getterCalls, b->didChangeCalls);
                fprintf(gLog, "CALLS glass-capsule     getter=%d didChange=%d\n", g1->getterCalls, g1->didChangeCalls);
                fprintf(gLog, "CALLS glass-concentric  getter=%d didChange=%d\n", g4->getterCalls, g4->didChangeCalls);
                fprintf(gLog, "GEOM root=%.1fx%.1f window=%.1fx%.1f\n", root.bounds.size.width, root.bounds.size.height, probeWin.frame.size.width, probeWin.frame.size.height);
                t9Shoot(&ctx, tag);
            }
#else
            fprintf(gLog, "scene corners needs SDK 27\n");
#endif
        } else {
            // ---- the product replica: window glass backdrop (Regular, no
            // contentView), terminal above it, sidebar strip, and the pill
            // parented the way ui_sidebar_tabs.mm parents it.
            NSGlassEffectView* backdrop = nil;
            if (strcmp(backdropKind, "blur") == 0) {
                T9BlurView* const frosted = [[T9BlurView alloc] initWithFrame:root.bounds];
                frosted.material = NSVisualEffectMaterialUnderWindowBackground;
                frosted.blendingMode = NSVisualEffectBlendingModeBehindWindow;
                frosted.state = NSVisualEffectStateActive;
                frosted.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
                [root addSubview:frosted];
            } else if (@available(macOS 26.0, *)) {
                backdrop = [[NSGlassEffectView alloc] initWithFrame:root.bounds];
                backdrop.style = NSGlassEffectViewStyleRegular;
                backdrop.translatesAutoresizingMaskIntoConstraints = YES;
                backdrop.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
                backdrop.cornerRadius = kWindowRadius;
                [root addSubview:backdrop];
            }
            T9TerminalView* const term = [[T9TerminalView alloc] initWithFrame:root.bounds];
            term->alpha = 0.30;   // -backgroundOpacity 30, so glass shows
            term.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
            [root addSubview:term];

            T9SidebarView* const side = [[T9SidebarView alloc] initWithFrame:NSMakeRect(0, 0, kSidebarW, kProbeH)];
            side->eatsClicks = !pillOnTop;
            side.autoresizingMask = NSViewHeightSizable;
            [root addSubview:side];

            const NSRect pillFlipped = t9PillFor(side.bounds, 1);
            const NSRect pillFrame = [root convertRect:pillFlipped fromView:side];
            [pillRects addObject:[NSValue valueWithRect:pillFrame]];

            if (!noPill) {
                if (@available(macOS 26.0, *)) {
                    NSGlassEffectView* const pill = [[NSGlassEffectView alloc] initWithFrame:pillFrame];
                    pill.style = NSGlassEffectViewStyleClear;
                    pill.translatesAutoresizingMaskIntoConstraints = YES;
                    pill.autoresizingMask = NSViewMinYMargin;
                    pill.tintColor = [NSColor colorWithSRGBRed:0.07 green:0.08 blue:0.11 alpha:0.65];
#if T9_SDK_27
                    if (newCorners) {
                        if (@available(macOS 27.0, *)) {
                            // A capsule cannot be asked for through the
                            // cornerRadius property; this is the point of the
                            // whole exercise.
                            T9GlassCornerView* const cap = [[T9GlassCornerView alloc] initWithFrame:pillFrame];
                            cap->wanted = [[NSViewCornerConfiguration capsuleCornerConfiguration] retain];
                            cap.style = NSGlassEffectViewStyleClear;
                            cap.translatesAutoresizingMaskIntoConstraints = YES;
                            cap.autoresizingMask = NSViewMinYMargin;
                            cap.tintColor = pill.tintColor;
                            if (interactive) { cap.effectIsInteractive = YES; }
                            if (pillOnTop) { [root addSubview:cap]; }
                            else { [root addSubview:cap positioned:NSWindowBelow relativeTo:side]; }
                            if (forward) { side->forwardTo = cap; }
                            goto placed;
                        }
                    }
                    if (interactive) {
                        if (@available(macOS 27.0, *)) { pill.effectIsInteractive = YES; }
                    }
#endif
                    pill.cornerRadius = kPillRadius;
                    if (pillOnTop) { [root addSubview:pill]; }
                    else { [root addSubview:pill positioned:NSWindowBelow relativeTo:side]; }
                    if (forward) { side->forwardTo = pill; }
                }
            }
        placed:
            (void)0;
            [probeWin makeKeyAndOrderFront:nil];
            [NSApp activateIgnoringOtherApps:YES];
            t9Settle(0.9);

            // Park the pointer off the window for the resting frame: a hover
            // response that is already showing would make "rest" a lie.
            const NSPoint parkBottomLeft = NSMakePoint(NSMinX(pf) - 80, NSMinY(pf) - 80);
            const CGPoint park = CGPointMake(parkBottomLeft.x, sf.size.height - parkBottomLeft.y);
            t9Move(park);
            t9Settle(0.35);
            {
                char nm[256];
                snprintf(nm, sizeof(nm), "%s-rest", tag);
                t9Shoot(&ctx, nm);
            }

            if (!noPill) {
                // Global top-left coordinates of the pill centre, from the
                // window server's rect and never from a cached frame (T6).
                CGRect wr;
                if (t9WindowRect((CGWindowID)([probeWin windowNumber]), &wr)) {
                    const NSRect pr = [[pillRects objectAtIndex:0] rectValue];
                    const CGPoint hit = CGPointMake(wr.origin.x + NSMidX(pr),
                                                    wr.origin.y + (kProbeH - NSMidY(pr)));
                    fprintf(gLog, "EVENT pill centre at %.0f,%.0f (window at %.0f,%.0f)\n",
                            hit.x, hit.y, wr.origin.x, wr.origin.y);
                    // An idle move first: the first pointing event after a
                    // window arrives is eaten (T6, measured).
                    t9Move(CGPointMake(hit.x - 4, hit.y - 4));
                    t9Settle(0.20);
                    t9Move(hit);
                    t9Settle(0.45);
                    {
                        char nm[256];
                        snprintf(nm, sizeof(nm), "%s-hover", tag);
                        t9Shoot(&ctx, nm);
                    }
                    t9Down(hit);
                    t9Settle(0.45);
                    {
                        char nm[256];
                        snprintf(nm, sizeof(nm), "%s-press", tag);
                        t9Shoot(&ctx, nm);
                    }
                    t9Up(hit);
                    t9Settle(0.20);
                    t9Move(park);
                }
            }
        }

        fprintf(gLog, "DONE shots=%d refused=%d\n", ctx.shots, ctx.refused);
        fflush(gLog);
        [probeWin orderOut:nil];
        [backWin orderOut:nil];
        return ctx.refused == 0 ? 0 : 2;
    }
}
