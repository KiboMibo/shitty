/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "ui_sidebar_tabs.h"

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
    struct SidebarTabsUi;
}

// The tab list itself: one flat row per session, top to bottom, and a
// new-tab row under them. Flat and tabs-only by the user's own call -
// no tree, no panes, no close glyphs. The view owns no model; it reads
// labels and the active index through its owner, which outlives it.
@interface ShittySidebarView: NSView {
    @public
    SidebarTabsUi* owner;
}
@end

namespace {
    struct CallSessionsChanged final: public Listener {
        explicit CallSessionsChanged(SidebarTabsUi* parent);

        void onListen(void*) override;

        SidebarTabsUi* parent;
    };

    // cmd+b. The one thing here that legitimately changes how many
    // columns the grid has (A7): the panel's width leaves the grid and
    // comes back, the shell hears a resize, and that is the intent -
    // this is a deliberate act by the user, the equivalent of dragging
    // the window edge. The hover strip T6 builds is the opposite case
    // and must not do this.
    struct CallToggleSidebar final: public Listener {
        explicit CallToggleSidebar(SidebarTabsUi* parent);

        void onListen(void*) override;

        SidebarTabsUi* parent;
    };

    // A reload can turn -sidebarTabs off, change -sidebarWidth, or
    // repaint the panel in new colors. All three are the same answer:
    // re-derive the reserve from the fresh snapshot and redraw. Without
    // it the option could be switched off and the grid would keep
    // paying for a panel nobody can see.
    struct CallConfigChanged final: public Listener {
        explicit CallConfigChanged(SidebarTabsUi* parent);

        void onListen(void*) override;

        SidebarTabsUi* parent;
    };

    // Same shape as CsdTabsUi (ui_csd_tabs.mm), and for the same
    // reason: the listeners fire on client fibers - the input pump
    // delivers cmd+b, the parser fiber delivers titles - and AppKit
    // layout has no business on a fiber stack. Everything AppKit is
    // deferred to the main queue; the fibers run on the main thread, so
    // the deferred block never races the snapshot it reads.
    struct SidebarTabsUi {
        explicit SidebarTabsUi(Composer& composer);

        void project();
        void apply();
        void applyReserve();
        void toggle();
        void configChanged();
        void tabSelected(size_t index);
        void tabOpened();
        bool shown() const;
        u16 widthPoints() const;
        NSWindow* nativeWindow() const;

        Composer& composer;
        CallSessionsChanged sessionsChanged{this};
        CallToggleSidebar toggleSidebar{this};
        CallConfigChanged configChanged_{this};
        ShittySidebarView* view = nil;
        // The projected model snapshot the view draws from.
        NSArray<NSString*>* labels = nil;
        size_t active = 0;
        // cmd+b's own state, and nothing else's: whether the user has
        // put the panel away. Whether it is on the screen at all is
        // this and -sidebarTabs together, which is what shown() is for.
        bool revealed = true;
        bool applyPending = false;
    };
}

CallSessionsChanged::CallSessionsChanged(SidebarTabsUi* parent_)
    : parent(parent_)
{
}

void CallSessionsChanged::onListen(void*) {
    parent->project();
}

CallToggleSidebar::CallToggleSidebar(SidebarTabsUi* parent_)
    : parent(parent_)
{
}

void CallToggleSidebar::onListen(void*) {
    parent->toggle();
}

CallConfigChanged::CallConfigChanged(SidebarTabsUi* parent_)
    : parent(parent_)
{
}

void CallConfigChanged::onListen(void*) {
    parent->configChanged();
}

namespace {
    // sRGB, the space the terminal itself renders in: the panel sits
    // against the grid and has to agree with it about what a color is.
    static NSColor* nsColorFromTerminalColor(Color color) {
        return [NSColor colorWithSRGBRed:color.red / 255.0 green:color.green / 255.0 blue:color.blue / 255.0 alpha:1.0];
    }

    // Points. The row height is the panel's own metric rather than a
    // multiple of the cell: the list is chrome, drawn by AppKit in
    // AppKit's units, and lining it up with a grid it does not overlap
    // would buy nothing.
    static const CGFloat shittySidebarRowHeight = 28;
    static const CGFloat shittySidebarTextInset = 12;
}

SidebarTabsUi::SidebarTabsUi(Composer& composer_)
    : composer(composer_)
{
    composer.sessionsChangedListeners.pushBack(&sessionsChanged);
    composer.toggleSidebarListeners.pushBack(&toggleSidebar);
    composer.configChangedListeners.pushBack(&configChanged_);
    // The reserve has to be in place before showWindow() sizes the grid
    // (application.cpp constructs this right after createWindow), or the
    // first frame would be laid out for a window with no panel in it and
    // resize a moment later.
    applyReserve();
    project();
}

u16 SidebarTabsUi::widthPoints() const {
    return composer.opts->sidebarWidth;
}

bool SidebarTabsUi::shown() const {
    return composer.opts->sidebarTabs && revealed;
}

NSWindow* SidebarTabsUi::nativeWindow() const {
    if (composer.window == nullptr) {
        return nil;
    }
    const plt::RenderContext context = composer.window->renderContext();
    // The backend tag, not the pointer: every backend hands back a
    // non-null .window, the headless one pointing at its own render
    // target, and sending that an Objective-C message takes the process
    // down (R2-qa round 2, B5). The single cast in this file lives
    // here, so every caller inherits the guard by asking for nil.
    if (context.backend != plt::RenderBackend::Cocoa) {
        return nil;
    }
    return (__bridge NSWindow*)(context.window);
}

void SidebarTabsUi::applyReserve() {
    // The width in points, straight from the option: Composer scales it
    // to backing pixels itself, so a display change needs nothing from
    // here. Zero when the panel is not on the screen - a reserve nobody
    // draws in is just columns taken away from the terminal.
    composer.setChromeReserve(ChromeSide::Right, shown() ? widthPoints() : 0);
}

void SidebarTabsUi::project() {
    SessionSet* const sessions = composer.sessions;
    if (sessions == nullptr) {
        return;
    }
    const size_t count = sessions->count();
    NSMutableArray<NSString*>* const next = [NSMutableArray arrayWithCapacity:(NSUInteger)(count)];
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

void SidebarTabsUi::apply() {
    applyPending = false;
    NSWindow* const window = nativeWindow();
    if (window == nil) {
        return;
    }
    NSView* const content = window.contentView;
    if (content == nil) {
        return;
    }
    if (!shown()) {
        if (view != nil) {
            [view removeFromSuperview];
            [view release];
            view = nil;
        }
        return;
    }
    const NSRect bounds = content.bounds;
    const CGFloat width = (CGFloat)(widthPoints());
    const NSRect frame = NSMakeRect(NSMaxX(bounds) - width, NSMinY(bounds), width, bounds.size.height);
    if (view == nil) {
        view = [[ShittySidebarView alloc] initWithFrame:frame];
        // Pinned to the right edge and as tall as the content: the same
        // strip Composer::contentInsets() keeps the grid out of, so the
        // two never disagree about where the terminal ends.
        view.autoresizingMask = NSViewMinXMargin | NSViewHeightSizable;
        view.wantsLayer = YES;
        view->owner = this;
        [content addSubview:view];
        if (composer.opts->verbose) {
            fprintf(stderr, "%s: sidebar: tab list installed on the right edge\n", composer.brand->identifierCString());
        }
    } else {
        view.frame = frame;
    }
    view.needsDisplay = YES;
}

void SidebarTabsUi::toggle() {
    if (!composer.opts->sidebarTabs) {
        // The chord is bound whether or not the option is: without the
        // panel there is nothing to show or hide, and swallowing cmd+b
        // to do nothing visible would be worse than passing it on.
        return;
    }
    revealed = !revealed;
    applyReserve();
    project();
    if (composer.window != nullptr) {
        composer.window->requestFrame();
    }
}

void SidebarTabsUi::configChanged() {
    applyReserve();
    project();
}

void SidebarTabsUi::tabSelected(size_t index) {
    SessionSet* const sessions = composer.sessions;
    if (sessions == nullptr || index >= sessions->count()) {
        return;
    }
    sessions->activate(index);
    composer.window->requestFrame();
}

void SidebarTabsUi::tabOpened() {
    SessionSet* const sessions = composer.sessions;
    if (sessions == nullptr) {
        return;
    }
    sessions->newSession();
    composer.window->requestFrame();
}

@implementation ShittySidebarView

// Row zero at the top, which is the only order a tab list reads in.
- (BOOL)isFlipped {
    return YES;
}

- (BOOL)mouseDownCanMoveWindow {
    return NO;
}

- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    const NSRect bounds = self.bounds;
    // The panel wears the terminal's own background, so it reads as
    // part of the window rather than as a floating control, and a
    // hairline down its leading edge says where the grid stops. Every
    // other color is mixed from opts->fg against that same background
    // rather than taken from the system label tiers, for the reason
    // spelled out at length in ui_csd_tabs.mm: the tiers are tuned for
    // the standard material and know nothing about opts->bg, and a
    // light system theme over a dark terminal background made them
    // effectively invisible (issue 84).
    NSColor* const background = nsColorFromTerminalColor(owner->composer.opts->bg);
    NSColor* const activeText = nsColorFromTerminalColor(owner->composer.opts->fg);
    NSColor* const accent = nsColorFromTerminalColor(owner->composer.opts->cr);
    NSColor* const idleText = [activeText blendedColorWithFraction:0.4 ofColor:background];
    NSColor* const hairline = [activeText blendedColorWithFraction:0.78 ofColor:background];
    NSColor* const activeFill = [activeText blendedColorWithFraction:0.88 ofColor:background];
    [background setFill];
    NSRectFill(bounds);
    [hairline setFill];
    NSRectFill(NSMakeRect(NSMinX(bounds), NSMinY(bounds), 1, bounds.size.height));

    NSMutableParagraphStyle* const style = [[[NSMutableParagraphStyle alloc] init] autorelease];
    // Long shell titles differ at the tail; keep it, iTerm style.
    style.lineBreakMode = NSLineBreakByTruncatingHead;
    NSFont* const font = [NSFont systemFontOfSize:[NSFont smallSystemFontSize]];
    NSDictionary* const activeAttributes = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: activeText,
        NSParagraphStyleAttributeName: style,
    };
    NSDictionary* const idleAttributes = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: idleText,
        NSParagraphStyleAttributeName: style,
    };

    NSArray<NSString*>* const labels = owner->labels;
    const NSUInteger count = labels.count;
    const NSUInteger active = (NSUInteger)(owner->active);
    for (NSUInteger at = 0; at < count; ++at) {
        const NSRect row = NSMakeRect(NSMinX(bounds), NSMinY(bounds) + shittySidebarRowHeight * (CGFloat)(at), bounds.size.width, shittySidebarRowHeight);
        if (NSMinY(row) >= NSMaxY(bounds)) {
            // A window too short for every tab shows the ones that fit;
            // the chords and the strip in the title bar reach the rest.
            break;
        }
        if (at == active) {
            // Two marks rather than one: a fill for the row and a
            // cursor-colored bar down its leading edge. cr is
            // guaranteed distinct from bg - the cursor would be
            // invisible in the grid otherwise - so the active tab is
            // still identifiable if the fill is too subtle on some
            // background, which is the whole job of this row.
            [activeFill setFill];
            NSRectFill(row);
            [accent setFill];
            NSRectFill(NSMakeRect(NSMinX(row), NSMinY(row), 2, row.size.height));
        }
        NSDictionary* const attributes = at == active ? activeAttributes : idleAttributes;
        NSString* const label = labels[at];
        const NSSize size = [label sizeWithAttributes:attributes];
        const CGFloat available = row.size.width - shittySidebarTextInset * 2;
        if (available <= 0) {
            continue;
        }
        const NSRect text = NSMakeRect(NSMinX(row) + shittySidebarTextInset, NSMinY(row) + (row.size.height - size.height) / 2, available, size.height);
        [label drawWithRect:text options:NSStringDrawingUsesLineFragmentOrigin attributes:attributes context:nil];
    }

    // The new-tab row, under the last tab: a plus on bare background,
    // with a hairline over it separating it from the list.
    const CGFloat plusTop = NSMinY(bounds) + shittySidebarRowHeight * (CGFloat)(count);
    if (plusTop + shittySidebarRowHeight > NSMaxY(bounds)) {
        return;
    }
    [hairline setFill];
    NSRectFill(NSMakeRect(NSMinX(bounds) + shittySidebarTextInset, plusTop, bounds.size.width - shittySidebarTextInset * 2, 1));
    NSString* const plus = @"+";
    const NSSize plusSize = [plus sizeWithAttributes:idleAttributes];
    [plus drawAtPoint:NSMakePoint(NSMinX(bounds) + shittySidebarTextInset, plusTop + (shittySidebarRowHeight - plusSize.height) / 2) withAttributes:idleAttributes];
}

- (void)mouseDown:(NSEvent*)event {
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    const NSUInteger count = owner->labels.count;
    const CGFloat offset = point.y - NSMinY(self.bounds);
    if (offset < 0) {
        return;
    }
    const NSUInteger row = (NSUInteger)(offset / shittySidebarRowHeight);
    if (row < count) {
        owner->tabSelected((size_t)(row));
        return;
    }
    if (row == count) {
        owner->tabOpened();
    }
    // Below the new-tab row is bare panel: it answers nothing, rather
    // than opening a tab for a click nowhere near the plus.
}

@end

void createSidebarTabsUi(ObjPool& owner, Composer& composer) {
    owner.make<SidebarTabsUi>(composer);
}
