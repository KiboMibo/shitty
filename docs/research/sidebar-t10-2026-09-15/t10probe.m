// T10 probe: the sidebar strip under glass - a tone toward fg, a soft edge, no
// seam. A cut-down of T9's t9probe.m (docs/research/glass-t9-2026-09-15): the
// same product replica, minus the corner matrix and the press sequence, plus
// the one view this task adds.
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
//
// Build:  clang -framework AppKit -framework QuartzCore -o t10probe t10probe.m
// Run:    ./t10probe --out out --x 40 --field 0.10 --alpha 0.04 --tone-over 1 --tag x

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Mirrors the pair lib/shitty/ui_sidebar_tabs.mm keeps around newer AppKit.
#if defined(MAC_OS_VERSION_27_0) && MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_VERSION_27_0
#define T10_SDK_27 1
#else
#define T10_SDK_27 0
#endif

// ---------------------------------------------------------------- geometry

// 760 rather than T9's 900: a 900-wide window cannot be placed on a 1728 pt
// screen without covering its centre, and see --x for what sat there.
static const CGFloat kProbeW = 760;
static const CGFloat kProbeH = 560;
// Sidebar metrics copied from lib/shitty/ui_sidebar_tabs.mm so the pill in
// this probe is the product's pill and not a lookalike.
static const CGFloat kSidebarW = 220;
static const CGFloat kRowH = 46;
static const CGFloat kListTop = 6;
static const CGFloat kPillInset = 6;
static const CGFloat kPillRadius = 6;
static const CGFloat kWindowRadius = 12;   // -quickCornerRadius default

// The theme the product's own example config ships (shitty.toml): bg and fg,
// the two colours every shade in the panel is mixed from.
static NSColor* t10Bg(CGFloat alpha) { return [NSColor colorWithSRGBRed:0.07 green:0.08 blue:0.11 alpha:alpha]; }
static NSColor* t10Fg(CGFloat alpha) { return [NSColor colorWithSRGBRed:0.92 green:0.93 blue:0.95 alpha:alpha]; }

// ------------------------------------------------------------ the backdrop

// A synthetic field: one flat colour plus a black hairline every 8 points.
// The flat part is what a mean is taken over; the hairlines prove a blur
// happened - they survive unblurred and vanish blurred (T3's rowvar).
@interface T10FieldView: NSView {
@public
    CGFloat level;
}
@end

@implementation T10FieldView
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

// ------------------------------------------------------ probe window content

@interface T10RootView: NSView
@end
@implementation T10RootView
- (BOOL)isOpaque { return NO; }
@end

// Stands in for the terminal's CAMetalLayer: a translucent flat fill over the
// whole window, which is what the glass backdrop has above it in the product.
@interface T10TerminalView: NSView {
@public
    CGFloat alpha;
}
@end
@implementation T10TerminalView
- (BOOL)isOpaque { return NO; }
- (NSView*)hitTest:(NSPoint)p { (void)p; return nil; }
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    [t10Bg(alpha) setFill];
    NSRectFill(self.bounds);
}
@end

// The sidebar strip: text and, when asked, the 1px black seam T6 left on the
// trailing edge. Flipped, like the product's panel.
@interface T10SidebarView: NSView {
@public
    BOOL seam;
    // When > 0 the strip paints the tone itself in drawRect:, i.e. OVER the
    // pill (the panel is above it) - the form the product shipped with
    // (ui_sidebar_tabs.mm, drawRect:, the glassSurface branch). The other
    // form, a tone view UNDER the pill, is t10ToneView below; the two were
    // measured against each other and this one won.
    CGFloat toneOver;
    CGFloat fade;
}
@end
@implementation T10SidebarView
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return NO; }
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    const NSRect b = self.bounds;
    if (toneOver > 0) {
        NSColor* const ink = t10Fg(toneOver);
        NSGradient* const ramp = [[NSGradient alloc] initWithColorsAndLocations:
            ink, 0.0, ink, (b.size.width - fade) / b.size.width, [ink colorWithAlphaComponent:0], 1.0, nil];
        [ramp drawInRect:b angle:0];
        [ramp release];
    }
    NSDictionary* const attrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:12],
        NSForegroundColorAttributeName: t10Fg(1.0),
    };
    NSDictionary* const dim = @{
        NSFontAttributeName: [NSFont systemFontOfSize:10],
        NSForegroundColorAttributeName: t10Fg(0.55),
    };
    for (int i = 0; i < 6; i += 1) {
        const CGFloat y = kListTop + kRowH * (CGFloat)(i);
        if (y + kRowH > NSMaxY(b)) { break; }
        [@"~/Projects/shitty" drawAtPoint:NSMakePoint(NSMinX(b) + 14, y + 7) withAttributes:attrs];
        [@"zsh - main" drawAtPoint:NSMakePoint(NSMinX(b) + 14, y + 25) withAttributes:dim];
    }
    if (seam) {
        [[NSColor colorWithSRGBRed:0 green:0 blue:0 alpha:0.70] setFill];
        NSRectFill(NSMakeRect(NSMaxX(b) - 1, NSMinY(b), 1, b.size.height));
    }
}
@end

// The pill rect of row `at`, in the panel's flipped coordinates. Same shape
// as sidebarPillFor() in the product, same numbers.
static NSRect t10PillFor(NSRect bounds, int at) {
    const NSRect row = NSMakeRect(NSMinX(bounds), NSMinY(bounds) + kListTop + kRowH * (CGFloat)(at), bounds.size.width, kRowH);
    return NSInsetRect(row, kPillInset, 2);
}

// The tone as a view UNDER the pill - the task's first form, not the one the
// product shipped: a plain layer-hosting NSView whose layer is a
// CAGradientLayer running left to right - fg at `alpha` over the whole strip,
// fading to fg at 0 over the last `fade` points before the terminal. Kept so
// the comparison stays reproducible: under, the Clear pill samples the tone
// through its bg tint and keeps about a third of it, and drops 7 units nearer
// the strip on a dark field; over (--tone-over 1), strip and pill move alike.
// Same colour at both ends of the fade, so the interpolation never passes
// through a darker premultiplied black.
static NSView* t10ToneView(NSRect frame, CGFloat alpha, CGFloat fade) {
    CAGradientLayer* const ramp = [CAGradientLayer layer];
    ramp.startPoint = CGPointMake(0, 0.5);
    ramp.endPoint = CGPointMake(1, 0.5);
    NSColor* const ink = t10Fg(alpha);
    ramp.colors = @[(id)(ink.CGColor), (id)(ink.CGColor), (id)([ink colorWithAlphaComponent:0].CGColor)];
    const CGFloat width = frame.size.width;
    ramp.locations = @[@0, @(width > fade ? (width - fade) / width : 0), @1];
    NSView* const tone = [[NSView alloc] initWithFrame:frame];
    tone.layer = ramp;
    tone.wantsLayer = YES;
    tone.autoresizingMask = NSViewHeightSizable;
    return tone;
}

// ------------------------------------------------------------ stack and shots

static FILE* gLog = NULL;

static BOOL t10WindowRect(CGWindowID wid, CGRect* out) {
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
static BOOL t10StackIsClean(CGWindowID mine, CGWindowID backdrop, CGRect capture) {
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

// A real AppKit pump, not -[NSRunLoop runUntilDate:] (T9: the run loop alone
// leaves posted events in the application's queue).
static void t10Settle(double seconds) {
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
} T10Ctx;

static BOOL t10Shoot(T10Ctx* ctx, const char* name) {
    t10Settle(0.40);
    CGRect r;
    if (!t10WindowRect((CGWindowID)([ctx->probeWin windowNumber]), &r)) {
        fprintf(gLog, "SHOT %s: no rect from the window server\n", name);
        ctx->refused += 1;
        return NO;
    }
    if (!t10StackIsClean((CGWindowID)([ctx->probeWin windowNumber]),
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

static void t10Move(CGPoint p) {
    CGEventRef const e = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, p, kCGMouseButtonLeft);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}

// ------------------------------------------------------------------- scene

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
        const double field = argNum(argc, argv, "--field", 0.10);
        const double alpha = argNum(argc, argv, "--alpha", 0.0);       // tone; 0 = no tone view at all
        const double fade = argNum(argc, argv, "--fade", 20.0);
        const int seam = (int)(argNum(argc, argv, "--seam", 0));
        const int toneOver = (int)(argNum(argc, argv, "--tone-over", 0));   // 1: the strip paints `alpha` itself, over the pill
        const int noPill = (int)(argNum(argc, argv, "--no-pill", 0));
        const double termAlpha = argNum(argc, argv, "--term-alpha", 0.60);   // -backgroundOpacity 60, the default
        const double tint = argNum(argc, argv, "--tint", 0.65);              // -sidebarTabTint 65, the default
        const char* const tag = argStr(argc, argv, "--tag", "frame");
        // Where the probe window's left edge goes; centred when absent. A
        // foreign 1x2 px window sat at the exact centre of this screen and
        // the stack check refused every centred shot, correctly.
        const double x = argNum(argc, argv, "--x", -1);

        NSString* const logPath = [NSString stringWithFormat:@"%s/%s.log", outdir, tag];
        gLog = fopen([logPath UTF8String], "w");
        if (gLog == NULL) { gLog = stderr; }
        setvbuf(gLog, NULL, _IOLBF, 0);
        fprintf(gLog, "T10 probe: field=%.2f alpha=%.3f fade=%.1f seam=%d toneOver=%d noPill=%d termAlpha=%.2f tint=%.2f SDK27=%d\n",
                field, alpha, fade, seam, toneOver, noPill, termAlpha, tint, T10_SDK_27);

        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

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
        T10FieldView* const fieldView = [[T10FieldView alloc] initWithFrame:NSMakeRect(0, 0, sf.size.width, sf.size.height)];
        fieldView->level = field;
        fieldView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        backWin.contentView = fieldView;
        [backWin setFrame:sf display:YES];
        [backWin orderFrontRegardless];

        // ---- the probe window, centred, well clear of every screen edge so
        // screencapture -R has nothing to clip silently.
        const NSRect pf = NSMakeRect(x < 0 ? NSMidX(sf) - kProbeW / 2 : x, NSMidY(sf) - kProbeH / 2, kProbeW, kProbeH);
        NSWindow* const probeWin = [[NSWindow alloc] initWithContentRect:pf
                                                              styleMask:NSWindowStyleMaskBorderless
                                                                backing:NSBackingStoreBuffered
                                                                  defer:NO];
        probeWin.opaque = NO;
        probeWin.backgroundColor = [NSColor clearColor];
        probeWin.hasShadow = NO;
        probeWin.level = NSNormalWindowLevel;
        T10RootView* const root = [[T10RootView alloc] initWithFrame:NSMakeRect(0, 0, kProbeW, kProbeH)];
        probeWin.contentView = root;

        T10Ctx ctx;
        ctx.backWin = backWin;
        ctx.probeWin = probeWin;
        ctx.outdir = outdir;
        ctx.shots = 0;
        ctx.refused = 0;

        // ---- the product replica, bottom to top: window glass backdrop
        // (Regular, no contentView), the terminal's fill, the strip's tone,
        // the pill, the strip - parented the way ui_sidebar_tabs.mm parents
        // them: tone and pill are siblings BELOW the panel, tone below pill.
        if (@available(macOS 26.0, *)) {
            NSGlassEffectView* const backdrop = [[NSGlassEffectView alloc] initWithFrame:root.bounds];
            backdrop.style = NSGlassEffectViewStyleRegular;
            backdrop.translatesAutoresizingMaskIntoConstraints = YES;
            backdrop.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
            backdrop.cornerRadius = kWindowRadius;
            [root addSubview:backdrop];
        }
        T10TerminalView* const term = [[T10TerminalView alloc] initWithFrame:root.bounds];
        term->alpha = termAlpha;
        term.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [root addSubview:term];

        T10SidebarView* const side = [[T10SidebarView alloc] initWithFrame:NSMakeRect(0, 0, kSidebarW, kProbeH)];
        side->seam = seam != 0;
        side->toneOver = toneOver ? alpha : 0;
        side->fade = fade;
        side.autoresizingMask = NSViewHeightSizable;
        [root addSubview:side];

        if (alpha > 0 && !toneOver) {
            [root addSubview:t10ToneView(side.frame, alpha, fade) positioned:NSWindowBelow relativeTo:side];
        }

        if (!noPill) {
            if (@available(macOS 26.0, *)) {
                const NSRect pillFrame = [root convertRect:t10PillFor(side.bounds, 1) fromView:side];
                NSGlassEffectView* const pill = [[NSGlassEffectView alloc] initWithFrame:pillFrame];
                pill.style = NSGlassEffectViewStyleClear;
                pill.cornerRadius = kPillRadius;
                pill.translatesAutoresizingMaskIntoConstraints = YES;
                pill.autoresizingMask = NSViewMinYMargin;
                pill.tintColor = t10Bg(tint);
#if T10_SDK_27
                if (@available(macOS 27.0, *)) { pill.effectIsInteractive = YES; }
#endif
                // Directly below the panel, which puts it above the tone
                // added a moment ago - the product's order.
                [root addSubview:pill positioned:NSWindowBelow relativeTo:side];
                [probeWin makeKeyAndOrderFront:nil];
                t10Settle(0.5);
#if T10_SDK_27
                if (@available(macOS 27.0, *)) {
                    // What the system resolved from cornerRadius alone, no
                    // configuration of ours: T9's tile 6 said 6, and this is
                    // the pill the product now builds on 27.
                    NSViewCornerRadii* const r = pill.effectiveCornerRadii;
                    fprintf(gLog, "RADII pill size=%.0fx%.0f config=%s effective=%s\n",
                            pill.frame.size.width, pill.frame.size.height,
                            pill.cornerConfiguration == nil ? "nil" : "set",
                            r == nil ? "nil" : [[NSString stringWithFormat:@"tl=%.2f tr=%.2f bl=%.2f br=%.2f", r.topLeft, r.topRight, r.bottomLeft, r.bottomRight] UTF8String]);
                }
#endif
            }
        }
        // The order actually in the tree, bottom to top, so the frame can be
        // read against a fact and not against the intent above.
        for (NSView* const v in root.subviews) {
            fprintf(gLog, "TREE %s %.0fx%.0f@%.0f,%.0f\n", object_getClassName(v),
                    v.frame.size.width, v.frame.size.height, v.frame.origin.x, v.frame.origin.y);
        }

        [probeWin makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        t10Settle(0.9);

        // Park the pointer off the window: nothing here hovers, but a pointer
        // over the window is one more thing a frame could differ by.
        const NSPoint parkBottomLeft = NSMakePoint(NSMinX(pf) - 80, NSMinY(pf) - 80);
        t10Move(CGPointMake(parkBottomLeft.x, sf.size.height - parkBottomLeft.y));
        t10Settle(0.35);
        fprintf(gLog, "GEOM root=%.1fx%.1f window=%.1fx%.1f sidebar=%.0f fade=%.0f\n",
                root.bounds.size.width, root.bounds.size.height, probeWin.frame.size.width, probeWin.frame.size.height,
                kSidebarW, fade);
        t10Shoot(&ctx, tag);

        fprintf(gLog, "DONE shots=%d refused=%d\n", ctx.shots, ctx.refused);
        fflush(gLog);
        [probeWin orderOut:nil];
        [backWin orderOut:nil];
        return ctx.refused == 0 ? 0 : 2;
    }
}
