/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "ui_csd_tabs.h"

#include "brand.h"
#include "composer.h"
#include "listener.h"
#include "options.h"
#include "session.h"

#include <plt/window.h>

#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>
#include <std/str/view.h>

#define Point MacLegacyPoint
#define Rect MacLegacyRect

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#undef Rect
#undef Point

#include <stdio.h>

using namespace stl;

namespace {
    struct CsdTabsUi;
}

// The iTerm2 look: the title bar itself, split into tabs by hairline
// separators. The view sits inside the titlebar container next to the
// traffic lights; the native title is hidden while it shows. The view
// owns no model - it reads labels and the active index through its
// owner, which outlives it.
@interface ShittyTabBarView: NSView {
    @public
    CsdTabsUi* owner;
}
@end

namespace {
    struct CallSessionsChanged final: public Listener {
        explicit CallSessionsChanged(CsdTabsUi* parent);

        void onListen(void*) override;

        CsdTabsUi* parent;
    };

    // Fires on every config reload (SIGUSR1), not just ones that touch
    // colors - the listener list carries no diff, so this repaints the
    // titlebar fill and the tab strip unconditionally. Reacting to an
    // OSC-driven background change from inside the running shell is a
    // separate, larger feature (the window would need to hear about it
    // at all) and stays out of scope here.
    struct CallConfigChanged final: public Listener {
        explicit CallConfigChanged(CsdTabsUi* parent);

        void onListen(void*) override;

        CsdTabsUi* parent;
    };

    // Listens to the tab model and mirrors it into the title bar. All
    // AppKit work runs on the main queue: the listener fires on client
    // fibers (the input pump delivers tab chords, the parser fiber
    // delivers titles), and AppKit layout has no business on a fiber
    // stack. The fibers themselves run on the main thread, so the
    // deferred block never races the snapshot it reads.
    struct CsdTabsUi {
        explicit CsdTabsUi(Composer& composer);

        void project();
        void apply();
        void applyTitlebarColor();
        void tabSelected(size_t index);
        void tabClosed(size_t index);
        void tabOpened();
        NSWindow* nativeWindow() const;

        Composer& composer;
        CallSessionsChanged sessionsChanged{this};
        CallConfigChanged configChanged{this};
        ShittyTabBarView* bar = nil;
        // The projected model snapshot the view draws from; nil hides
        // the strip (a lone session keeps the clean native title).
        NSArray<NSString*>* labels = nil;
        size_t active = 0;
        bool applyPending = false;
    };
}

CallSessionsChanged::CallSessionsChanged(CsdTabsUi* parent_)
    : parent(parent_)
{
}

void CallSessionsChanged::onListen(void*) {
    parent->project();
}

CallConfigChanged::CallConfigChanged(CsdTabsUi* parent_)
    : parent(parent_)
{
}

void CallConfigChanged::onListen(void*) {
    parent->applyTitlebarColor();
}

namespace {
    // sRGB, the space the terminal itself renders in: a calibrated color
    // would land beside the grid it is supposed to continue or blend into.
    static NSColor* nsColorFromTerminalColor(Color color) {
        return [NSColor colorWithSRGBRed:color.red / 255.0 green:color.green / 255.0 blue:color.blue / 255.0 alpha:1.0];
    }
}

CsdTabsUi::CsdTabsUi(Composer& composer_)
    : composer(composer_)
{
    composer.sessionsChangedListeners.pushBack(&sessionsChanged);
    composer.configChangedListeners.pushBack(&configChanged);
    // The window already exists by the time application.cpp constructs
    // this object (createCsdTabsUi runs right after createWindow), so the
    // initial color applies here instead of waiting for the first reload.
    applyTitlebarColor();
}

NSWindow* CsdTabsUi::nativeWindow() const {
    if (composer.window == nullptr) {
        return nil;
    }
    return (__bridge NSWindow*)(composer.window->renderContext().window);
}

void CsdTabsUi::project() {
    SessionSet* const sessions = composer.sessions;
    if (sessions == nullptr || composer.window == nullptr) {
        return;
    }
    const size_t count = sessions->count();
    NSMutableArray<NSString*>* next = nil;
    if (count >= 2) {
        next = [NSMutableArray arrayWithCapacity:(NSUInteger)(count)];
        for (size_t at = 0; at < count; ++at) {
            // A tab whose shell never set a title shows the brand name,
            // like a fresh window does.
            StringView title = sessions->title(at);
            if (title.length() == 0) {
                title = composer.brand->displayName();
            }
            Buffer label(title);
            NSString* const text = [NSString stringWithUTF8String:label.cStr()];
            [next addObject:text == nil ? @"" : text];
        }
    }
    [next retain];
    [labels release];
    labels = next;
    active = sessions->activeIndex();
    if (applyPending) {
        return;
    }
    applyPending = true;
    dispatch_async(dispatch_get_main_queue(), ^{
        apply();
    });
}

void CsdTabsUi::apply() {
    applyPending = false;
    NSWindow* const window = nativeWindow();
    if (window == nil) {
        if (composer.opts->verbose) {
            fprintf(stderr, "%s: tabs: no native window in the render context\n", composer.brand->identifierCString());
        }
        return;
    }
    if (labels == nil) {
        if (bar != nil) {
            [bar removeFromSuperview];
            [bar release];
            bar = nil;
            window.titleVisibility = NSWindowTitleVisible;
            if (@available(macOS 11.0, *)) {
                window.titlebarSeparatorStyle = NSTitlebarSeparatorStyleAutomatic;
            }
        }
        return;
    }
    NSButton* const zoom = [window standardWindowButton:NSWindowZoomButton];
    NSView* const titlebar = zoom != nil ? zoom.superview : nil;
    if (titlebar == nil) {
        if (composer.opts->verbose) {
            fprintf(stderr, "%s: tabs: no titlebar container to draw into\n", composer.brand->identifierCString());
        }
        return;
    }
    // Up to the very window edge: the trailing new-tab cell is never
    // filled, so nothing opaque reaches the rounded corner. The gap
    // before the first tab is bare title bar outside this view, so it
    // drags the window natively, double-click zoom included.
    const CGFloat left = NSMaxX(zoom.frame) + 56;
    const NSRect frame = NSMakeRect(left, 0, titlebar.bounds.size.width - left, titlebar.bounds.size.height);
    if (bar == nil) {
        bar = [[ShittyTabBarView alloc] initWithFrame:frame];
        bar.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        bar->owner = this;
        [titlebar addSubview:bar];
        window.titleVisibility = NSWindowTitleHidden;
        // The automatic style draws a hard rule under the title bar on
        // some releases - straight through the seam where the active tab
        // continues into its terminal.
        if (@available(macOS 11.0, *)) {
            window.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;
        }
        if (composer.opts->verbose) {
            fprintf(stderr, "%s: tabs: strip installed over the title bar\n", composer.brand->identifierCString());
        }
    } else {
        bar.frame = frame;
    }
    bar.needsDisplay = YES;
}

void CsdTabsUi::applyTitlebarColor() {
    NSWindow* const window = nativeWindow();
    if (window == nil) {
        return;
    }
    // no-decorations makes the option meaningless (there is no title bar
    // to blend): leave the window's default background alone rather than
    // paint a borderless window's whole frame.
    if (!composer.opts->transparentTitlebar || composer.opts->noDecorations) {
        return;
    }
    window.backgroundColor = nsColorFromTerminalColor(composer.opts->bg);
    // Every color drawRect: mixes in this mode - the accent bar and the
    // idle text/hairline blends alike - reads opts->bg or opts->fg
    // straight from composer.opts, so a reload that only changes colors
    // still needs a repaint even though the tab model itself (project())
    // never fired. Only reachable here, under transparentTitlebar: the
    // plain title bar's active fill and system label colors already
    // track window state AppKit repaints on its own.
    bar.needsDisplay = YES;
}

void CsdTabsUi::tabSelected(size_t index) {
    SessionSet* const sessions = composer.sessions;
    if (sessions == nullptr || index >= sessions->count()) {
        return;
    }
    sessions->activate(index);
    composer.window->requestFrame();
}

void CsdTabsUi::tabClosed(size_t index) {
    SessionSet* const sessions = composer.sessions;
    if (sessions == nullptr || index >= sessions->count()) {
        return;
    }
    if (sessions->close(index)) {
        composer.window->requestFrame();
    } else {
        // The strip only shows with two or more tabs, so this is
        // unreachable in practice; the chord path's semantics anyway.
        composer.window->requestClose();
    }
}

void CsdTabsUi::tabOpened() {
    SessionSet* const sessions = composer.sessions;
    if (sessions == nullptr) {
        return;
    }
    sessions->newSession();
    composer.window->requestFrame();
}

// The trailing new-tab cell is square-ish; everything left of it is
// split evenly between the tabs. The close glyph answers clicks in a
// fixed leading zone of each tab.
static const CGFloat shittyTabPlusWidth = 34;
static const CGFloat shittyTabCloseZone = 24;

@implementation ShittyTabBarView

- (BOOL)mouseDownCanMoveWindow {
    return NO;
}

- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    NSArray<NSString*>* const labels = owner->labels;
    const NSUInteger count = labels.count;
    if (count == 0) {
        return;
    }
    const NSUInteger active = (NSUInteger)(owner->active);
    const NSRect bounds = self.bounds;
    const CGFloat tabsWidth = bounds.size.width - shittyTabPlusWidth;
    const CGFloat cellWidth = tabsWidth / (CGFloat)(count);
    // With an opaque title bar, the active tab is a piece of the terminal
    // it fronts: its cell wears the terminal's background and foreground,
    // and idle tabs stay bare so the title bar's own material shows
    // through. transparentTitlebar paints that same background onto the
    // title bar itself (WindowImpl's titlebarAppearsTransparent plus
    // CsdTabsUi::applyTitlebarColor), so the fill that used to set the
    // active tab apart would now match its surroundings and vanish; the
    // accent bar below stands in for it instead.
    const bool transparentTitlebar = owner->composer.opts->transparentTitlebar;
    NSColor* const activeFill = nsColorFromTerminalColor(owner->composer.opts->bg);
    NSColor* const activeText = nsColorFromTerminalColor(owner->composer.opts->fg);
    NSColor* const activeGlyphs = [activeText colorWithAlphaComponent:0.75];
    // Only the transparentTitlebar branch below ever fills with this.
    NSColor* const activeAccent = transparentTitlebar ? nsColorFromTerminalColor(owner->composer.opts->cr) : nil;
    // The strip is our own surface, and the system label tiers are tuned
    // for controls on the standard material: tertiary label over a dark
    // title bar measures 1.16:1 against it (issue 84), which is nothing.
    // Every idle tier moves one step up, and the hairlines are mixed from
    // the label color so they keep following the appearance.
    //
    // That fix assumed the title bar's own material stays in step with
    // NSAppearance, which transparentTitlebar breaks: the title bar now
    // wears opts->bg, a color the window's appearance (and so the system
    // label tiers) knows nothing about. A user on a light system theme
    // with the project's own default dark bg measured out at 1.60:1 -
    // effectively invisible, and the hairlines marking tab boundaries
    // disappeared with it, since they are mixed from the same tier.
    // Flipping window.appearance by opts->bg's brightness would drag the
    // tiers back in step, but they would still be blind to opts->bg's
    // actual color, same as the pre-fix issue 84 case; every idle color
    // here is mixed straight from opts->fg and opts->bg instead, so idle
    // tabs are legible by construction against the exact bg painted
    // above, independent of the system theme entirely - not just brought
    // back in step with it. The active tab keeps the full fg strength;
    // idle tabs sit further toward bg so the active one reads as the
    // brighter of the two without a fill to say so anymore, and the
    // hairline sits furthest toward bg of the three, same low-emphasis
    // role the alpha-faded label color played before.
    NSColor* const idleText = transparentTitlebar ? [activeText blendedColorWithFraction:0.4 ofColor:activeFill] : NSColor.labelColor;
    NSColor* const idleGlyphs = transparentTitlebar ? [activeText blendedColorWithFraction:0.55 ofColor:activeFill] : NSColor.secondaryLabelColor;
    NSColor* const hairline = transparentTitlebar ? [activeText blendedColorWithFraction:0.68 ofColor:activeFill] : [NSColor.labelColor colorWithAlphaComponent:0.4];
    NSMutableParagraphStyle* const centered = [[[NSMutableParagraphStyle alloc] init] autorelease];
    centered.alignment = NSTextAlignmentCenter;
    // Long shell titles differ at the tail; keep it, iTerm style.
    centered.lineBreakMode = NSLineBreakByTruncatingHead;
    NSFont* const activeFont = [NSFont titleBarFontOfSize:0];
    NSFont* const idleFont = [NSFont systemFontOfSize:activeFont.pointSize];
    NSDictionary* const activeAttributes = @{
        NSFontAttributeName: activeFont,
        NSForegroundColorAttributeName: activeText,
        NSParagraphStyleAttributeName: centered,
    };
    NSDictionary* const idleAttributes = @{
        NSFontAttributeName: idleFont,
        NSForegroundColorAttributeName: idleText,
        NSParagraphStyleAttributeName: centered,
    };
    NSDictionary* const activeGlyphAttributes = @{
        NSFontAttributeName: activeFont,
        NSForegroundColorAttributeName: activeGlyphs,
    };
    NSDictionary* const idleGlyphAttributes = @{
        NSFontAttributeName: idleFont,
        NSForegroundColorAttributeName: idleGlyphs,
    };
    const auto drawGlyph = [&](NSString* glyph, CGFloat x, NSDictionary* attributes) {
        const NSSize size = [glyph sizeWithAttributes:attributes];
        [glyph drawAtPoint:NSMakePoint(x, bounds.origin.y + (bounds.size.height - size.height) / 2) withAttributes:attributes];
    };
    for (NSUInteger at = 0; at < count; ++at) {
        const NSRect cell = NSMakeRect(bounds.origin.x + cellWidth * (CGFloat)(at), bounds.origin.y, cellWidth, bounds.size.height);
        if (at == active) {
            if (transparentTitlebar) {
                // A solid opts->bg fill would be invisible against the
                // now-matching title bar behind it, so the active tab is
                // marked with a cursor-colored bar along its bottom edge
                // instead. cr is guaranteed distinct from bg - the cursor
                // itself would be invisible in the grid otherwise - but
                // not from fg: options.cpp defaults cr to fg when unset,
                // in which case the bar simply matches the active text,
                // which is harmless here. Either way it reads the same
                // way the terminal's own cursor does: "the active thing
                // is here".
                [activeAccent setFill];
                NSRectFill(NSMakeRect(cell.origin.x, cell.origin.y, cell.size.width, 2));
            } else {
                // Everything but the top point: that row is where the
                // window's own frame edge lives, and an opaque fill over
                // it darkens the border itself rather than the title bar.
                [activeFill setFill];
                NSRectFill(NSMakeRect(cell.origin.x, cell.origin.y, cell.size.width, cell.size.height - 1));
            }
        }
        // Hairlines separate bare cells only; the active tab draws its
        // own edge - a solid fill in the plain title bar, the accent
        // bar's own left/right extent under transparentTitlebar (see
        // above). A hairline touching either would double up on that
        // edge and read as a wall boxing the active tab in, which is
        // exactly what the accent bar alone is meant to avoid. The
        // leftmost tab has the drag gap to its left, and that seam wants
        // the same line unless the tab itself is the marked one.
        if (at != active && (at == 0 || at - 1 != active)) {
            [hairline setFill];
            NSRectFillUsingOperation(NSMakeRect(cell.origin.x, cell.origin.y + 4, 1, cell.size.height - 8), NSCompositingOperationSourceOver);
        }
        NSDictionary* const glyphAttributes = at == active ? activeGlyphAttributes : idleGlyphAttributes;
        drawGlyph(@"\u00d7", cell.origin.x + 9, glyphAttributes);
        CGFloat trailing = 8;
        if (at < 9) {
            NSString* const hint = [NSString stringWithFormat:@"\u2318%u", (unsigned)(at + 1)];
            const NSSize hintSize = [hint sizeWithAttributes:glyphAttributes];
            trailing += hintSize.width + 8;
            drawGlyph(hint, NSMaxX(cell) - 8 - hintSize.width, glyphAttributes);
        }
        NSDictionary* const attributes = at == active ? activeAttributes : idleAttributes;
        NSString* const label = labels[at];
        const NSSize size = [label sizeWithAttributes:attributes];
        const CGFloat leading = shittyTabCloseZone;
        const CGFloat available = cell.size.width - leading - trailing;
        if (available <= 0) {
            continue;
        }
        const NSRect text = NSMakeRect(cell.origin.x + leading, cell.origin.y + (cell.size.height - size.height) / 2, available, size.height);
        [label drawWithRect:text options:NSStringDrawingUsesLineFragmentOrigin attributes:attributes context:nil];
    }
    // The trailing new-tab cell: bare material, a plus, and a hairline
    // against the last tab unless the active tab already draws that edge
    // itself - same reasoning as the loop above.
    if (count - 1 != active) {
        [hairline setFill];
        NSRectFillUsingOperation(NSMakeRect(bounds.origin.x + tabsWidth, bounds.origin.y + 4, 1, bounds.size.height - 8), NSCompositingOperationSourceOver);
    }
    NSString* const plus = @"+";
    const NSSize plusSize = [plus sizeWithAttributes:idleGlyphAttributes];
    drawGlyph(plus, bounds.origin.x + tabsWidth + (shittyTabPlusWidth - plusSize.width) / 2, idleGlyphAttributes);
}

- (void)mouseDown:(NSEvent*)event {
    const NSUInteger count = owner->labels.count;
    if (count == 0) {
        return;
    }
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    const NSRect bounds = self.bounds;
    const CGFloat tabsWidth = bounds.size.width - shittyTabPlusWidth;
    if (point.x >= bounds.origin.x + tabsWidth) {
        owner->tabOpened();
        return;
    }
    const CGFloat cellWidth = tabsWidth / (CGFloat)(count);
    NSUInteger index = (NSUInteger)((point.x - bounds.origin.x) / cellWidth);
    if (index >= count) {
        index = count - 1;
    }
    if (point.x - bounds.origin.x - cellWidth * (CGFloat)(index) < shittyTabCloseZone) {
        owner->tabClosed((size_t)(index));
        return;
    }
    owner->tabSelected((size_t)(index));
}

@end

void createCsdTabsUi(ObjPool& owner, Composer& composer) {
    owner.make<CsdTabsUi>(composer);
}
