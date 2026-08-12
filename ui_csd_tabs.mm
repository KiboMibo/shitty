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
        void tabSelected(size_t index);
        NSWindow* nativeWindow() const;

        Composer& composer;
        CallSessionsChanged sessionsChanged{this};
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

CsdTabsUi::CsdTabsUi(Composer& composer_)
    : composer(composer_)
{
    composer.sessionsChangedListeners.pushBack(&sessionsChanged);
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
        }
        return;
    }
    if (bar == nil) {
        NSButton* const zoom = [window standardWindowButton:NSWindowZoomButton];
        NSView* const titlebar = zoom != nil ? zoom.superview : nil;
        if (titlebar == nil) {
            if (composer.opts->verbose) {
                fprintf(stderr, "%s: tabs: no titlebar container to draw into\n", composer.brand->identifierCString());
            }
            return;
        }
        const CGFloat left = NSMaxX(zoom.frame) + 8;
        bar = [[ShittyTabBarView alloc] initWithFrame:NSMakeRect(left, 0, titlebar.bounds.size.width - left - 8, titlebar.bounds.size.height)];
        bar.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        bar->owner = this;
        [titlebar addSubview:bar];
        window.titleVisibility = NSWindowTitleHidden;
        if (composer.opts->verbose) {
            fprintf(stderr, "%s: tabs: strip installed over the title bar\n", composer.brand->identifierCString());
        }
    }
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
    const CGFloat cellWidth = bounds.size.width / (CGFloat)(count);
    // The active tab is a piece of the terminal it fronts: its cell
    // wears the terminal's background and foreground. Idle tabs stay
    // bare, so the title bar's own material shows through.
    const Color terminalBackground = owner->composer.opts->bg;
    const Color terminalForeground = owner->composer.opts->fg;
    NSColor* const activeFill = [NSColor colorWithCalibratedRed:terminalBackground.red / 255.0 green:terminalBackground.green / 255.0 blue:terminalBackground.blue / 255.0 alpha:1.0];
    NSColor* const activeText = [NSColor colorWithCalibratedRed:terminalForeground.red / 255.0 green:terminalForeground.green / 255.0 blue:terminalForeground.blue / 255.0 alpha:1.0];
    NSDictionary* const activeAttributes = @{
        NSFontAttributeName: [NSFont titleBarFontOfSize:0],
        NSForegroundColorAttributeName: activeText,
    };
    NSDictionary* const idleAttributes = @{
        NSFontAttributeName: [NSFont titleBarFontOfSize:0],
        NSForegroundColorAttributeName: NSColor.secondaryLabelColor,
    };
    for (NSUInteger at = 0; at < count; ++at) {
        const NSRect cell = NSMakeRect(bounds.origin.x + cellWidth * (CGFloat)(at), bounds.origin.y, cellWidth, bounds.size.height);
        if (at == active) {
            [activeFill setFill];
            NSRectFill(cell);
        }
        if (at != 0) {
            [NSColor.separatorColor setFill];
            NSRectFillUsingOperation(NSMakeRect(cell.origin.x, cell.origin.y + 4, 1, cell.size.height - 8), NSCompositingOperationSourceOver);
        }
        NSDictionary* const attributes = at == active ? activeAttributes : idleAttributes;
        NSString* const label = labels[at];
        NSSize size = [label sizeWithAttributes:attributes];
        const CGFloat margin = 12;
        const CGFloat available = cell.size.width - 2 * margin;
        if (available <= 0) {
            continue;
        }
        NSRect text = NSMakeRect(
            cell.origin.x + margin + (available > size.width ? (available - size.width) / 2 : 0),
            cell.origin.y + (cell.size.height - size.height) / 2,
            available > size.width ? size.width : available,
            size.height
        );
        [label drawWithRect:text options:NSStringDrawingUsesLineFragmentOrigin attributes:attributes context:nil];
    }
}

- (void)mouseDown:(NSEvent*)event {
    const NSUInteger count = owner->labels.count;
    if (count == 0) {
        return;
    }
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    NSUInteger index = (NSUInteger)(point.x / (self.bounds.size.width / (CGFloat)(count)));
    if (index >= count) {
        index = count - 1;
    }
    owner->tabSelected((size_t)(index));
}

@end

void createCsdTabsUi(ObjPool& owner, Composer& composer) {
    owner.make<CsdTabsUi>(composer);
}
