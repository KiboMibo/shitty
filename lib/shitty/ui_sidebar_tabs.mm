/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "ui_sidebar_tabs.h"

#include "tint_coat.h"

#include "brand.h"
#include "composer.h"
#include <lib/vterm/listener.h>
#include "options.h"
#include "process_directory.h"
#include "session.h"
#include "tab_rows.h"
#include "bookmarks.h"
#include "bookmark_probe.h"

#include <plt/window.h>

#include <std/alg/minmax.h>
#include <std/ios/fs_utils.h>
#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/throw.h>

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define Point MacLegacyPoint
#define Rect MacLegacyRect

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>

#undef Rect
#undef Point

// After AppKit, and it has to be: see the header.
#include "ui_window_tint.h"

#include <stdio.h>

// @available guards the runtime; building against an older SDK also needs the
// declarations to exist at all. Same pair, same spelling, as the one
// ext/plt/platform_cocoa.mm keeps around NSGlassEffectView itself.
#if defined(MAC_OS_VERSION_26_0) && MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_VERSION_26_0
    #define UI_SDK_MACOS_26 1
#else
    #define UI_SDK_MACOS_26 0
#endif

#if defined(MAC_OS_VERSION_27_0) && MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_VERSION_27_0
    #define UI_SDK_MACOS_27 1
#else
    #define UI_SDK_MACOS_27 0
#endif

using namespace stl;

namespace {
    struct SidebarTabsUi;
}

// The tab list itself: one row per pane, top to bottom, and a new-tab row
// under them. A tab of one pane is one plain row; a split tab is a group of
// rows in one frame, so no pane of it hides behind the focused one - the
// user lost panes that way before (tab_rows.h). Still no tree and no close
// glyphs. The view owns no model; it reads labels, the rows and the active
// row through its owner, which outlives it.
@interface TerminalSidebarView: NSView {
    @public
    SidebarTabsUi* owner;
    @private
    // The row under the pointer, and whether there is one at all. Two
    // fields rather than a sentinel index because Objective-C zeroes
    // ivars, and a zeroed sentinel would light up row zero before the
    // pointer ever entered the panel.
    NSUInteger hoverRow;
    BOOL hovering;
    NSTrackingArea* tracking;
    // A row being dragged: the row the press landed on (-1 for none),
    // where it landed, and whether the pointer has gone far enough for
    // this to be a drag rather than a click. While it is one, where it
    // would land: on a folder's label, or into the gap above row
    // dropIndex (rows.length() for the end).
    long long pressRow;
    NSPoint pressPoint;
    BOOL dragging;
    BOOL dropOnLabel;
    NSUInteger dropIndex;
    // The pop-over a shut folder shows while the pointer is on its label,
    // the folder it is for, and the rows it lists - its buttons' tags index
    // them.
    NSPopover* folderPopover;
    NSString* popoverFolder;
    stl::Vector<TabRow>* popoverRows;
}
- (long long)rowAtPoint:(NSPoint)point;
- (void)newTabFromMenu:(id)sender;
- (void)newFolderFromMenu:(id)sender;
- (void)showPopoverForRow:(size_t)row;
- (void)closePopover;
- (void)menuMoveToFolder:(NSMenuItem*)item;
- (void)menuMoveToNewFolder:(NSMenuItem*)item;
- (void)menuRemoveFromFolder:(NSMenuItem*)item;
- (void)menuPin:(NSMenuItem*)item;
- (void)menuClose:(NSMenuItem*)item;
- (void)menuToggle:(NSMenuItem*)item;
- (void)menuRename:(NSMenuItem*)item;
- (void)menuRenameTab:(NSMenuItem*)item;
- (void)menuIcon:(NSMenuItem*)item;
- (void)menuDeleteFolder:(NSMenuItem*)item;
- (void)popoverPick:(NSInteger)index;
- (void)peekCheck;
- (BOOL)popoverShown;
@end

// The layered window's panel title bar: the sidebar's toggle at its
// leading edge and the active tab's title across the middle, over the
// band Composer keeps the grid out of. A subview of the content view, so
// it draws over the terminal's layer; the rest of it drags the window.
// The pop-over of a shut folder: its tabs and bookmarks, one line each,
// the one under the pointer lifted, and "New Tab" under a hairline. Drawn
// rather than built from buttons, so its rows have the list's own air and
// a hover of their own.
@interface TerminalFolderPopoverView: NSView {
    @public
    TerminalSidebarView* sidebar;
    NSArray<NSString*>* titles;
    NSInteger hover;
    NSTrackingArea* tracking;
}
- (NSInteger)indexAtPoint:(NSPoint)point;
@end

// The strip along the window's left edge that brings a hidden sidebar out
// while the pointer is on it. It takes no clicks - hitTest: answers nil, so
// whatever is under it keeps them - and only its tracking area matters.
@interface TerminalEdgeView: NSView {
    @public
    SidebarTabsUi* owner;
    @private
    NSTrackingArea* tracking;
}
@end

@interface TerminalPanelHeaderView: NSView {
    @public
    SidebarTabsUi* owner;
    // Points from the view's leading edge to the toggle: clear of the
    // window's standard buttons when the panel runs under them.
    CGFloat leading;
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

    // The layered window's frame follows the surface: the panel, the
    // hole in the surface under it and the clip on its corners all have
    // to move when the window does, and a grid re-count is when the
    // panel's rectangle changes.
    struct CallResized final: public Listener {
        explicit CallResized(SidebarTabsUi* parent);

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
        void applyPill();
        void dropPill();
        void applyReserve();
        void applyLayers();
        void dropLayers();
        bool layered() const;
        CGFloat listInset() const;
        void toggle();
        void configChanged();
        void rowSelected(size_t row);
        // The pin in a row's gutter: pins the row's tab, or unpins the
        // row's bookmark, writing bookmarks.toml either way.
        void rowPinned(size_t row);
        // Whether the row carries the pin at all: a bookmark's head row,
        // or the first row of an ordinary tab.
        bool rowPinnable(size_t row) const;
        // Where a bookmark row's bookmark stands. A host that does not
        // answer is said only while nothing runs there: an open session
        // is its own proof the host is up.
        BookmarkState rowState(size_t row) const;
        // Folders (variant C on the canvas): a label row stands for one.
        // A click shuts or opens it; a double click renames it.
        void folderToggled(size_t row);
        void folderToggledByName(stl::StringView folder);
        // The context menu's other verbs.
        void rowClosed(size_t row);
        void folderIconChosen(stl::StringView folder, stl::StringView icon);
        // A folder's members as rows, shut or not: what the pop-over of a
        // shut folder lists.
        void folderMembers(stl::StringView folder, stl::Vector<TabRow>& out) const;
        // What a row is called in a menu or a pop-over.
        NSString* rowTitle(const TabRow& row) const;
        // The "+" row's menu: a new folder, named so it is new, and at once
        // being renamed.
        stl::StringView folderCreated();
        // Renaming: the folder's label becomes a text field; committing
        // renames the window's folder and moves the bookmarks naming it,
        // in their file.
        void beginRename(stl::StringView folder);
        void commitRename(NSString* text);
        // Names a tab or a bookmark from a sheet: a bookmark's name is
        // saved in its file, an ordinary tab's lasts as long as the window.
        void beginRenameTab(size_t row);
        // Deleting a folder: at once when nothing is in it, else after a
        // sheet asking whether its tabs and bookmarks are let go of - kept,
        // out of any folder - or closed and taken out of the file.
        void beginDeleteFolder(stl::StringView folder);
        void commitDeleteFolder(stl::StringView folder, bool closeTabs);
        // A row dragged in the list and let go: into `folder`, before the
        // tab `before` when that is one of the folder's (count() for the end).
        void rowDropped(size_t row, stl::StringView folder, size_t before);
        // Where a row is, in the panel's (flipped) coordinates: the one
        // sum of row heights the drawing, the pill of glass, the hover and
        // the click all use. `at` may be the "+" row, rows.length().
        NSRect rowRect(NSRect bounds, size_t at) const;
        void tabOpened();
        // The sidebar put away with cmd+b, brought out over the terminal
        // while the pointer is at the window's left edge (the canvas's
        // EdgeReveal board): it floats, the grid keeps its columns, and the
        // window's buttons come with it. It goes when the pointer has left
        // it for peekDelay, or when a row in it is picked.
        void peek();
        void peekCheck();
        void endPeek();
        void endPeekSoon();
        // The list is in the window: docked (shown()) or peeking.
        bool listed() const;
        // Only the layered window hides its buttons and has an edge to
        // hover: the other modes keep a title bar the buttons belong to.
        void applyWindowButtons(NSWindow* window);
        void applyEdge(NSWindow* window);
        NSColor* surfaceColor() const;
        bool shown() const;
        u16 widthPoints() const;
        NSWindow* nativeWindow() const;

        Composer& composer;
        CallSessionsChanged sessionsChanged{this};
        CallToggleSidebar toggleSidebar{this};
        CallConfigChanged configChanged_{this};
        CallResized resized{this};
        TerminalSidebarView* view = nil;
        // The layered window's three pieces, all nil while it is off. The
        // surface is the lower layer - a sheet of -sidebarColor over the
        // window's backdrop, with the panel cut out of it - and the glass
        // is the upper one, the panel's own sheet, present only where the
        // backdrop is glass. Both live in the frame view below the content
        // view, the one place that is under the terminal (platform_cocoa.mm
        // puts the backdrop there for the same reason). The clip is the
        // mask on the content view's layer that rounds the panel's corners.
        NSView* surface = nil;
        NSView* panelGlass = nil;
        CAShapeLayer* clip = nil;
        // The panel's own title bar, over the terminal's top band; and the
        // shadow and hairline the panel casts on the surface, which are
        // sublayers of the surface's layer and go with it.
        TerminalPanelHeaderView* header = nil;
        CALayer* shadow = nil;
        CAShapeLayer* edge = nil;
        bool layersPending = false;
        // The active row's floating pill of glass, or nil when the window
        // has no glass backdrop for it to stand on. It lies under `view`
        // and inside the content view, over the strip the renderer clears.
        // Typed NSView* so the field needs no availability annotation of
        // its own, the way WindowImpl::glassBackdrop is
        // (ext/plt/platform_cocoa.mm).
        NSView* pill = nil;
        // The projected model snapshot the view draws from.
        NSArray<NSString*>* labels = nil;
        // The other two lines of every row, in step with labels by
        // index. Empty means "nothing known", which is not the same as
        // the branch line's "no git" and must not render as it.
        NSArray<NSString*>* folders = nil;
        NSArray<NSString*>* branches = nil;
        // The row the selection is on: the active tab's focused pane. A
        // row index, not a tab index - with split groups a tab is as many
        // rows as it has panes.
        size_t active = 0;
        // Which tab and pane every row stands for, in step with labels.
        stl::Vector<TabRow> rows;
        // Every row's height, in step with rows: a folder's label is short.
        stl::Vector<double> heights;
        // The folders the user has shut, by name, for the window's life.
        stl::Vector<stl::StringView> collapsed;
        // The folder being renamed while its sheet is up.
        stl::StringView renaming;
        // cmd+b's own state, and nothing else's: whether the user has
        // put the panel away. Whether it is on the screen at all is
        // this and -sidebarTabs together, which is what shown() is for.
        bool revealed = true;
        bool peeking = false;
        TerminalEdgeView* edgeZone = nil;
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

CallResized::CallResized(SidebarTabsUi* parent_)
    : parent(parent_)
{
}

void CallResized::onListen(void*) {
    // Deferred like everything AppKit here, and coalesced: a live resize
    // re-counts the grid on every step, and one pass per turn of the main
    // queue is all the frames can use.
    if (parent->layersPending || parent->surface == nil) {
        return;
    }
    parent->layersPending = true;
    SidebarTabsUi* const owner = parent;
    dispatch_async(dispatch_get_main_queue(), ^{
        owner->applyLayers();
    });
}

namespace {
    NSString* sidebarText(StringView view) {
        Buffer buffer(view);
        NSString* const text = [NSString stringWithUTF8String:buffer.cStr()];
        return text == nil ? @"" : text;
    }

    // Whether the window is backed by the system's glass, asked of the
    // live view hierarchy and deliberately not re-derived from the
    // options.
    //
    // Same discipline as windowTintAlpha() next door, and for a stronger
    // reason. Three things decide this and only one of them is an
    // option: -backgroundBlur glass, a system that has NSGlassEffectView
    // at all, and -backgroundOpacity below 100. All three are weighed in
    // ext/plt/platform_cocoa.mm, which then either installs the glass or
    // silently falls back to the frosted pane; asking the option here
    // would answer "glass" on a machine that can never show any, and the
    // chrome would stand its own paint down for a backdrop that is not
    // there.
    //
    // The backdrop is a sibling of the content view under the frame view
    // - the one placement that puts it below the terminal without moving
    // window.contentView - so this looks exactly where it is put.
    static bool windowBackdropIsGlass(NSWindow* window) {
#if UI_SDK_MACOS_26
        if (@available(macOS 26.0, *)) {
            NSView* const content = window == nil ? nil : window.contentView;
            NSView* const frame = content == nil ? nil : content.superview;
            for (NSView* const sibling in frame.subviews) {
                if ([sibling isKindOfClass:[NSGlassEffectView class]]) {
                    return true;
                }
            }
        }
#else
        (void)window;
#endif
        return false;
    }

    // sRGB, the space the terminal itself renders in: the panel sits
    // against the grid and has to agree with it about what a color is.
    static NSColor* nsColorFromTerminalColor(Color color, CGFloat alpha = 1.0) {
        return [NSColor colorWithSRGBRed:color.red / 255.0 green:color.green / 255.0 blue:color.blue / 255.0 alpha:alpha];
    }

    // How far the panel sits from the terminal's own background: six
    // percent of the foreground mixed into it. Named once because S10
    // spends it twice - as a mix when the window is opaque, and as the
    // alpha of an overlay when it is not - and the two are the same
    // panel only while they are the same number.
    static const CGFloat sidebarPanelTint = 0.06;

    // The bytes behind an NSColor, in the space everything here is built
    // in. Needed because the panel's default colour is mixed by AppKit
    // and the coat arithmetic (tint_coat.h) works on Color: computing
    // that mix a second time in integers would put the two a byte or so
    // apart, and the point of the default is that nothing moves.
    static Color terminalColorFromNsColor(NSColor* color) {
        NSColor* const srgb = [color colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
        if (srgb == nil) {
            return {0, 0, 0};
        }
        const CGFloat scale = 255.0;
        return {
            (u8)(srgb.redComponent * scale + 0.5),
            (u8)(srgb.greenComponent * scale + 0.5),
            (u8)(srgb.blueComponent * scale + 0.5),
        };
    }

    // Every shade in the panel is opts->fg mixed into opts->bg by this
    // one function, so the whole list is one ramp over the terminal's
    // own two colors and stays legible on any theme - see drawRect: for
    // why the system label tiers are not used.
    static NSColor* sidebarMix(NSColor* background, NSColor* foreground, CGFloat fraction) {
        return [background blendedColorWithFraction:fraction ofColor:foreground];
    }

    // Points. The row height is the panel's own metric rather than a
    // multiple of the cell: the list is chrome, drawn by AppKit in
    // AppKit's units, and lining it up with a grid it does not overlap
    // would buy nothing.
    // Three stacked lines - what is running, which folder, which branch -
    // plus the padding above and below them. Derived rather than written
    // down, so the row can never be too short for what it draws.
    static const CGFloat sidebarRowPad = 7;
    static const CGFloat sidebarTitleLine = 15;
    static const CGFloat sidebarSubLine = 13;
    static const CGFloat sidebarRowHeight = sidebarRowPad * 2 + sidebarTitleLine + sidebarSubLine * 2;
    // A folder's label row: one line of small capitals, the canvas's 22
    // points and a little air.
    static const CGFloat sidebarLabelRowHeight = 24;
    // The pop-over of a shut folder: its width, its rows, the air round
    // them, and the gap the hairline over "New Tab" sits in.
    static const CGFloat sidebarPopoverWidth = 260;
    static const CGFloat sidebarPopoverRow = 28;
    static const CGFloat sidebarPopoverPad = 6;
    static const CGFloat sidebarPopoverRule = 9;
    // The icons a folder can take from its context menu: SF Symbols, so
    // they are there whatever font the terminal uses.
    static NSString* const sidebarFolderIcons[] = {
        @"folder", @"terminal", @"server.rack", @"globe", @"network", @"cloud",
        @"hammer", @"wrench.and.screwdriver", @"gearshape", @"doc.text", @"star",
        @"bolt", @"flame", @"cube", @"briefcase", @"house",
    };
    // The gap above the first row, so the list does not start flush
    // against the window's top edge.
    static const CGFloat sidebarListTop = 6;
    // How far the active/hovered pill stays clear of the panel's edges,
    // and how far the text sits inside the pill.
    static const CGFloat sidebarPillInset = 6;
    static const CGFloat sidebarTextInset = sidebarPillInset + 10;
    // The number gutter: cmd+1..9 select tabs (InputActions::SelectTab1
    // and on), so the row says which digit it answers to. Past nine
    // there is no chord and the gutter is left empty rather than filled
    // with a number that does nothing.
    static const CGFloat sidebarNumberGutter = 18;
    // The icon column on the folder and branch lines. One width for both,
    // so the two texts share a left edge whatever is drawn to the left of
    // them; zero when the glyphs are not in the font at all.
    static const CGFloat sidebarIconColumn = 18;
    // Nerd Font code points, the user's own pick: nf-fa-folder and
    // nf-dev-git_branch. Both are in the Private Use Area and both are
    // single UTF-16 units, so a UniChar carries either whole.
    static const unichar sidebarFolderIcon = 0xF07B;
    static const unichar sidebarBranchIcon = 0xE725;
    static const CGFloat sidebarPillRadius = 6;
    // The layered window's flat selection, rounder than the glass pill:
    // the mock's 10, beside a panel whose own corners are 12.
    static const CGFloat sidebarLayeredPillRadius = 10;
    // Points of air between a split group's frame and the pills of its
    // rows.
    static const CGFloat sidebarGroupInset = 3;
    // Points. The group's map of its split: its size, how far in from the
    // frame's top-right corner it sits, and the air between its cells -
    // the canvas's 22 by 16 with a 2 point gap.
    static const CGFloat sidebarGroupMapWidth = 22;
    static const CGFloat sidebarGroupMapHeight = 16;
    static const CGFloat sidebarGroupMapRight = 7;
    static const CGFloat sidebarGroupMapTop = 5;
    static const CGFloat sidebarGroupMapGap = 2;
    // Bookmarks: the status dot at the head row's trailing edge, filled
    // while the bookmark is open; a closed one has none and its row is
    // drawn at the dim tier. The dot's colour is the canvas's "alive"
    // green. The gutter's glyph is an SF Symbol, drawn where it is.
    static const CGFloat sidebarBookmarkDot = 7;
    static const CGFloat sidebarBookmarkDotGap = 6;
    // The strip's own tone under glass, flat across the whole strip (T10
    // ended it in a fade; the user asked for the gradient to go). It lives
    // only under glass: in blur and off the strip paints its panel colour
    // and a hairline, and the layered window paints nothing here at all.
    //
    // The tone is opts->fg at this alpha over the strip - toward fg, not
    // "lighter": on a dark theme fg is light and the strip lightens, on a
    // light theme fg is dark and the strip greys, which is what Finder does
    // on either side of the switch, and one rule serves both. Measured on a
    // probe (docs/research/sidebar-t10-2026-09-15): 0.04 lifts the strip
    // against the terminal by 9.9% on a dark field and 10.9% on a light one
    // - the contrast ratio strip:terminal, the same arithmetic as the 4.5:1
    // the text is held to - with the active title still at 13.4:1 and
    // 9.5:1 over it and 13.9:1 / 13.0:1 on the pill. Finder's own sidebar
    // sits about 15% over its content.
    static const CGFloat sidebarGlassTone = 0.04;
    // Points. The air between the layered window's panel edge and its
    // text, on every side: the border option is the air around the text
    // inside a pane, and this is the panel's own, so the text does not
    // sit against a rounded corner. Added to the grid's insets through
    // Composer::setPanelLayer(), never drawn.
    static const u16 sidebarPanelPad = 8;
    // Points. The layered window's two title bars: the panel's own band at
    // its top, which the grid starts below, and how far down the sidebar's
    // list starts so that its first row clears the window's standard
    // buttons, which sit on the sidebar's surface there for good.
    static const u16 sidebarPanelHeader = 36;
    static const CGFloat sidebarLayeredListTop = 40;
    // Where the standard buttons end, in points from the window's left
    // edge: the panel's title bar starts its own button past them when the
    // sidebar is away and the panel runs under them.
    static const CGFloat sidebarWindowButtonsRight = 80;
    // Points. The hidden sidebar's edge strip, how far past its edge the
    // pointer may stray and keep it, the corners of its free edge, how
    // opaque it is over the terminal, and how long it stays once the
    // pointer has left it. It comes out flush with the window's left, top
    // and bottom edges, where it docks: the window's buttons stand where
    // AppKit puts them, and an inset sheet left them in its very corner.
    static const CGFloat sidebarPeekZone = 6;
    static const CGFloat sidebarPeekSlack = 6;
    static const CGFloat sidebarPeekRadius = 12;
    static const CGFloat sidebarPeekOpacity = 0.94;
    static const NSTimeInterval sidebarPeekDelay = 0.3;
    // The panel's shadow on the surface and the hairline round its edge,
    // the mock's `-10px 0 28px rgba(0,0,0,.28)` and 1px at 8%: CSS blur is
    // twice Core Animation's shadowRadius.
    static const CGFloat sidebarPanelShadowOpacity = 0.28;
    static const CGFloat sidebarPanelShadowRadius = 14;
    static const CGFloat sidebarPanelShadowOffset = -10;
    static const CGFloat sidebarPanelEdgeAlpha = 0.08;

    // The pill of one row, in the panel's own (flipped) coordinates, or an
    // empty rect for a row the panel is too short to draw whole.
    //
    // Shared for the same reason sidebarTabsRowAt() is: the pill is now drawn
    // two ways - a fill in drawRect: and, when the window carries glass, a
    // floating sheet parented beside the panel - and a sheet a few points off
    // the row it belongs to would be a defect nothing else could catch.
    static NSRect sidebarPillFor(NSRect bounds, NSRect row) {
        if (NSMaxY(row) > NSMaxY(bounds)) {
            // A window too short for every tab shows the ones that fit whole;
            // half a row drawn at the bottom edge is what a list like this
            // must never look like, and half a pill of glass even less so.
            return NSZeroRect;
        }
        // Inset from both edges rather than full-bleed: it is what says "one
        // row of a list" instead of "the panel changed colour here".
        return NSInsetRect(row, sidebarPillInset, 2);
    }
}

// What a row shows instead of the raw window title, and the row a click
// lands in. Both are plain functions of plain types, and both are
// non-static and declared again in ui_sidebar_tabs_ut.cpp, for the
// reason F4 hoisted csdTabsChromeAlpha() out of ui_csd_tabs.mm: inside
// drawRect: or an NSEvent handler no headless test can reach them, and
// that is exactly how an inverted decision stayed green through a whole
// suite once already (R4-test, N13).

// Shells set the title to the whole of
// "user@host:~/Projects/github.com/shitty", which in a 220pt column
// truncates to "...ects/github.com/shitty" - the complaint this
// replaces. The last path component is the part that differs between
// tabs, and it is what iTerm2 and Ghostty show too. A title with no
// slash in it is a command line and is left alone; so is one ending in
// a slash, where the component would be empty.
StringView sidebarTabsShortTitle(StringView title) {
    const size_t length = title.length();
    if (length == 0 || title[length - 1] == '/') {
        return title;
    }
    for (size_t at = length; at > 0; --at) {
        if (title[at - 1] == '/') {
            return title.suffix(length - at);
        }
    }
    return title;
}

// The git branch a row shows on its third line, and the two pure halves
// of working it out. Both are non-static and declared again in
// ui_sidebar_tabs_ut.cpp for the reason the one above is: a decision
// reachable only through the filesystem is a decision no test pins down.
//
// False here means "a directory, and no repository above it". That is
// deliberately a different answer from processDirectory()'s false, "no
// directory to be had" - no such process, or one this user may not
// inspect: the row renders the first as "no git" and the second as
// nothing at all, so an empty line can never stand for both at once.

// ".git" is a directory in an ordinary clone and a *file* in a linked
// worktree, holding "gitdir: <path>\n". Returns that path, or an empty
// view when the contents are not a link. Not a hypothetical case: this
// repository has half a dozen linked worktrees open right now, and the
// panel is being built inside one of them.
StringView sidebarTabsGitDirLink(StringView contents) {
    static const StringView marker(u8"gitdir: ");
    if (!contents.startsWith(marker)) {
        return StringView();
    }
    StringView path = contents.suffix(contents.length() - marker.length());
    while (path.length() != 0 && (path.back() == '\n' || path.back() == '\r' || path.back() == ' ')) {
        path = path.prefix(path.length() - 1);
    }
    return path;
}

// HEAD is "ref: refs/heads/<branch>\n" while a branch is checked out and
// the bare object id when the head is detached. Returns the branch name,
// or the id abbreviated the way git itself abbreviates it, or an empty
// view when the file is neither - a corrupt or half-written HEAD reads
// as "no repository" rather than as a row of garbage.
StringView sidebarTabsHeadBranch(StringView head) {
    while (head.length() != 0 && (head.back() == '\n' || head.back() == '\r' || head.back() == ' ')) {
        head = head.prefix(head.length() - 1);
    }
    static const StringView ref(u8"ref: ");
    if (head.startsWith(ref)) {
        StringView name = head.suffix(head.length() - ref.length());
        // A symbolic HEAD normally points into refs/heads/; anything
        // else is shown as written rather than guessed at.
        static const StringView heads(u8"refs/heads/");
        if (name.startsWith(heads)) {
            name = name.suffix(name.length() - heads.length());
        }
        return name;
    }
    // Seven characters is git's own abbreviation, and a whole object id
    // would not fit the column anyway.
    if (head.length() < 7) {
        return StringView();
    }
    for (size_t at = 0; at < head.length(); ++at) {
        const u8 ch = head[at];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))) {
            return StringView();
        }
    }
    return head.prefix(7);
}

namespace {
    void appendPath(Buffer& out, StringView directory, StringView leaf) {
        out.reset();
        out.append(directory.data(), directory.length());
        if (directory.length() != 0 && directory.back() != '/') {
            static const StringView slash(u8"/");
            out.append(slash.data(), 1);
        }
        out.append(leaf.data(), leaf.length());
    }

    // Reads a small file if it is there. The house idiom is
    // readFileContent() inside a catch (quick_frame_store.cpp), and that
    // stays - but existence is a stat rather than a caught throw,
    // because walking up from a directory outside any repository would
    // otherwise raise once per level, per tab, per projection.
    bool readSmallFile(Buffer& path, Buffer& out) {
        struct stat info;
        if (::stat(path.cStr(), &info) != 0 || !S_ISREG(info.st_mode)) {
            return false;
        }
        out.reset();
        try {
            readFileContent(path, out);
        } catch (Exception&) {
            return false;
        }
        return true;
    }
}

// Walks up from `directory` to the first .git, resolves a worktree link
// if that is what it turns out to be, and writes the branch into `out`.
// False means "no repository above this directory", which the row shows
// as "no git" - and which the caller must not confuse with "not looked
// yet": a row that has never been resolved shows neither.
bool sidebarTabsBranch(StringView directory, Buffer& out) {
    out.reset();
    // Absolute paths only. A relative one would be resolved against this
    // process's directory, which is not the tab's, and would answer with
    // a branch belonging to somebody else entirely.
    if (directory.length() == 0 || directory[0] != '/') {
        return false;
    }
    Buffer path;
    Buffer contents;
    StringView here = directory;
    for (;;) {
        while (here.length() > 1 && here.back() == '/') {
            here = here.prefix(here.length() - 1);
        }
        static const StringView dotGit(u8".git");
        appendPath(path, here, dotGit);
        struct stat info;
        if (::stat(path.cStr(), &info) == 0) {
            Buffer gitDir;
            if (S_ISDIR(info.st_mode)) {
                gitDir.append(path.data(), path.used());
            } else if (readSmallFile(path, contents)) {
                const StringView link = sidebarTabsGitDirLink(StringView(contents));
                if (link.length() == 0) {
                    return false;
                }
                if (link[0] == '/') {
                    gitDir.append(link.data(), link.length());
                } else {
                    // Submodules write the link relative to the
                    // directory holding it; worktrees write it absolute.
                    appendPath(gitDir, here, link);
                }
            } else {
                return false;
            }
            static const StringView head(u8"HEAD");
            appendPath(path, StringView(gitDir), head);
            if (!readSmallFile(path, contents)) {
                return false;
            }
            const StringView branch = sidebarTabsHeadBranch(StringView(contents));
            if (branch.length() == 0) {
                return false;
            }
            out.append(branch.data(), branch.length());
            return true;
        }
        if (here.length() <= 1) {
            return false;
        }
        size_t cut = here.length();
        while (cut > 1 && here[cut - 1] != '/') {
            --cut;
        }
        here = here.prefix(cut == 1 ? 1 : cut - 1);
    }
}

// One of a row's three lines, measured down from the row's own top edge:
// 0 is what is running, 1 the folder, 2 the git branch. Drawing and the
// row height come out of the same arithmetic, which is what stops a row
// being too short for its own contents - the defect a written-down height
// invites the moment a line's size changes.
double sidebarTabsLineTop(size_t line) {
    return sidebarRowPad + (line == 0 ? 0 : sidebarTitleLine + sidebarSubLine * (double)(line - 1));
}

double sidebarTabsLineHeight(size_t line) {
    return line == 0 ? sidebarTitleLine : sidebarSubLine;
}

double sidebarTabsRowHeight() {
    return sidebarRowHeight;
}

double sidebarTabsListTop() {
    return sidebarListTop;
}

// The panel draws its text in the system font, which has no Private Use
// Area at all, so the icons have to come from somewhere else. That
// somewhere is the terminal's own font (-font, which the user already
// points at a Nerd Font to see these glyphs in the grid) and only for
// the two icon glyphs - the labels stay in the system font, because a
// monospace face reads badly as UI text. No second option for a panel
// font: it would be a second thing to configure that answers the same
// question the first one already did.
//
// Nothing is drawn on a guess. CTFontGetGlyphsForCharacters is the same
// question font_coretext.cpp:807 already asks of a face, and a face that
// answers no gets no icon - not a hollow box, which is what a font
// without the glyph would otherwise paint.
NSFont* sidebarFontCovering(StringView fontName, unichar codepoint, CGFloat size) {
    if (fontName.length() == 0) {
        return nil;
    }
    Buffer name(fontName);
    NSString* const family = [NSString stringWithUTF8String:name.cStr()];
    if (family == nil) {
        return nil;
    }
    NSFont* const font = [NSFont fontWithName:family size:size];
    if (font == nil) {
        return nil;
    }
    CGGlyph glyph = 0;
    if (!CTFontGetGlyphsForCharacters((__bridge CTFontRef)(font), &codepoint, &glyph, 1) || glyph == 0) {
        return nil;
    }
    return font;
}

namespace {
    // The one call that puts an icon on the screen, shared by the row and
    // by the test that measures whether anything landed. A nil font draws
    // nothing at all - that is the whole decision, and it is here rather
    // than at the call site so it cannot be made twice and differently.
    // An SF Symbol in one colour, fitted into `box` with its own aspect
    // and centred: the pin a hovered row offers in its gutter. A system
    // image rather than a Nerd Font glyph, because the pin is a control
    // and has to be there whatever font the terminal uses.
    // A name asked for in a sheet on the window. Not an edit in place: a
    // field inside the list has to win the keyboard from the terminal
    // under it, and the sheet is where AppKit gives the keyboard away by
    // itself. Return renames, Escape leaves things as they were.
    void sidebarAskName(NSWindow* window, NSString* title, NSString* initial, void (^done)(NSString*)) {
        NSAlert* const alert = [[[NSAlert alloc] init] autorelease];
        alert.messageText = title;
        [alert addButtonWithTitle:@"Rename"];
        [alert addButtonWithTitle:@"Cancel"];
        NSTextField* const field = [[[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 260, 24)] autorelease];
        field.stringValue = initial != nil ? initial : @"";
        alert.accessoryView = field;
        [alert layout];
        alert.window.initialFirstResponder = field;
        void (^kept)(NSString*) = [[done copy] autorelease];
        [alert beginSheetModalForWindow:window completionHandler:^(NSModalResponse response) {
            if (response == NSAlertFirstButtonReturn) {
                kept(field.stringValue);
            }
        }];
        [alert.window makeFirstResponder:field];
    }

    void sidebarDrawSymbol(NSString* name, NSRect box, NSColor* color) {
        NSImage* const symbol = [NSImage imageWithSystemSymbolName:name accessibilityDescription:nil];
        if (symbol == nil || !(symbol.size.width > 0) || !(symbol.size.height > 0)) {
            return;
        }
        const CGFloat scale = min(box.size.width / symbol.size.width, box.size.height / symbol.size.height);
        const NSSize size = NSMakeSize(symbol.size.width * scale, symbol.size.height * scale);
        const NSRect fitted = NSMakeRect(NSMidX(box) - size.width / 2, NSMidY(box) - size.height / 2, size.width, size.height);
        NSImage* const tinted = [NSImage imageWithSize:size flipped:NO drawingHandler:^BOOL(NSRect rect) {
            [symbol drawInRect:rect];
            [color set];
            NSRectFillUsingOperation(rect, NSCompositingOperationSourceAtop);
            return YES;
        }];
        [tinted drawInRect:fitted fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
    }

    void sidebarDrawIcon(NSFont* font, unichar codepoint, NSPoint at, NSColor* color) {
        if (font == nil) {
            return;
        }
        NSString* const text = [NSString stringWithCharacters:&codepoint length:1];
        [text drawAtPoint:at withAttributes:@{NSFontAttributeName: font, NSForegroundColorAttributeName: color}];
    }
}

// How much ink one icon leaves, drawn through the call above into an
// offscreen bitmap. Zero means nothing was drawn - which is a different
// answer from a hollow replacement box, and telling those two apart is
// the only way a test can say the icons are really there.
unsigned sidebarTabsIconInk(StringView fontName, unsigned codepoint, double size) {
    const NSInteger side = (NSInteger)(size * 3) + 4;
    NSBitmapImageRep* const rep = [[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:side pixelsHigh:side bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0] autorelease];
    if (rep == nil) {
        return 0;
    }
    NSGraphicsContext* const context = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
    if (context == nil) {
        return 0;
    }
    [NSGraphicsContext saveGraphicsState];
    [NSGraphicsContext setCurrentContext:context];
    [[NSColor clearColor] set];
    NSRectFill(NSMakeRect(0, 0, (CGFloat)(side), (CGFloat)(side)));
    sidebarDrawIcon(sidebarFontCovering(fontName, (unichar)(codepoint), (CGFloat)(size)), (unichar)(codepoint), NSMakePoint(2, 2), NSColor.blackColor);
    [context flushGraphics];
    [NSGraphicsContext restoreGraphicsState];
    unsigned char* const pixels = rep.bitmapData;
    if (pixels == nullptr) {
        return 0;
    }
    unsigned inked = 0;
    const NSInteger stride = rep.bytesPerRow;
    for (NSInteger y = 0; y < side; ++y) {
        for (NSInteger x = 0; x < side; ++x) {
            if (pixels[y * stride + x * 4 + 3] != 0) {
                ++inked;
            }
        }
    }
    return inked;
}

// Whether the face named carries the code point at all, for a test that
// wants the question without the drawing.
bool sidebarTabsFontCovers(StringView fontName, unsigned codepoint) {
    return sidebarFontCovering(fontName, (unichar)(codepoint), 13) != nil;
}

// Where a row's line starts. Line 0 is the title and sits flush; the
// folder and branch lines share one indent, whether or not an icon is
// drawn in it - which is what keeps them aligned with each other when
// the font has no glyphs and the column collapses to nothing.
double sidebarTabsLineLeft(size_t line, double textLeft, bool iconsAvailable) {
    if (line == 0) {
        return textLeft;
    }
    return textLeft + (iconsAvailable ? sidebarIconColumn : 0);
}

// The row an offset down from the panel's top edge falls in: an index
// into the list, `count` for the new-tab row under it, or -1 for panel
// that answers nothing. One function, so drawing and clicking can never
// disagree about where a row is - including at the bottom edge, where a
// row that does not fit whole is drawn nowhere and so answers nothing
// either.
// How far down from the list's top a row starts: the heights of the rows
// above it. Rows past `count` - the "+" row - are the ordinary height, and
// so is every row when `heights` is null.
double sidebarTabsRowOffset(const double* heights, size_t count, size_t at) {
    double offset = 0;
    for (size_t row = 0; row < at; ++row) {
        offset += heights != nullptr && row < count ? heights[row] : sidebarRowHeight;
    }
    return offset;
}

// The row an offset down from the panel's top edge falls in: an index
// into the list, `count` for the new-tab row under it, or -1 for panel
// that answers nothing. One function, so drawing and clicking can never
// disagree about where a row is - including at the bottom edge, where a
// row that does not fit whole is drawn nowhere and so answers nothing
// either. Rows are as tall as `heights` says (a folder's label is short);
// null is every row the ordinary height.
long long sidebarTabsRowAtHeights(double panelHeight, double offsetFromTop, const double* heights, size_t count, double topInset) {
    // C10: `topInset` is how far down the list starts, which is no
    // longer the top of the panel. The panel now runs the whole height
    // of the window and the title bar is drawn over its top; the rows
    // must not be, or the first one sits under the window buttons where
    // it cannot be read and can barely be clicked.
    //
    // It arrives here rather than being applied by each caller because
    // that is the whole reason this function exists: drawing and
    // clicking share it so they cannot disagree about where a row is,
    // and two call sites each subtracting their own inset is exactly how
    // they would start to.
    const double listTop = topInset + sidebarListTop;
    if (offsetFromTop < listTop) {
        return -1;
    }
    double top = listTop;
    for (size_t row = 0; row <= count; ++row) {
        const double height = heights != nullptr && row < count ? heights[row] : sidebarRowHeight;
        if (offsetFromTop < top + height) {
            return top + height <= panelHeight ? (long long)(row) : -1;
        }
        top += height;
    }
    return -1;
}

long long sidebarTabsRowAt(double panelHeight, double offsetFromTop, size_t count, double topInset) {
    return sidebarTabsRowAtHeights(panelHeight, offsetFromTop, nullptr, count, topInset);
}

NSRect SidebarTabsUi::rowRect(NSRect bounds, size_t at) const {
    const double* const all = heights.length() != 0 ? heights.data() : nullptr;
    const size_t count = heights.length();
    const CGFloat top = NSMinY(bounds) + listInset() + sidebarListTop + (CGFloat)(sidebarTabsRowOffset(all, count, at));
    const CGFloat height = all != nullptr && at < count ? (CGFloat)(heights[at]) : sidebarRowHeight;
    return NSMakeRect(NSMinX(bounds), top, bounds.size.width, height);
}

// Whether an offset in from the panel's leading edge is on a row's pin,
// which stands in the number gutter while the pointer is over the row.
// One function for the drawing and the click, like sidebarTabsRowAt().
bool sidebarTabsPinAt(double offsetFromLeft) {
    return offsetFromLeft >= sidebarPillInset && offsetFromLeft < sidebarTextInset + sidebarNumberGutter;
}

SidebarTabsUi::SidebarTabsUi(Composer& composer_)
    : composer(composer_)
{
    composer.sessionsChangedListeners.pushBack(&sessionsChanged);
    composer.toggleSidebarListeners.pushBack(&toggleSidebar);
    composer.configChangedListeners.pushBack(&configChanged_);
    composer.resizedListeners.pushBack(&resized);
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

bool SidebarTabsUi::listed() const {
    return composer.opts->sidebarTabs && (revealed || peeking);
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
    //
    // The left edge, which is also the signal ui_csd_tabs.mm reads to
    // decide the title-bar strip is redundant (V2): a non-zero reserve
    // on this side means a tab list is already on the screen.
    composer.setChromeReserve(ChromeSide::Left, shown() ? widthPoints() : 0);
    // The layered window moves the grid off every edge the same way, and
    // it moves with the reserve: cmd+b hides the list and the panel takes
    // the gap on the left instead of the sidebar's width.
    composer.setPanelLayer(layered(), composer.opts->panelGap, sidebarPanelPad, sidebarPanelHeader);
}

void SidebarTabsUi::project() {
    SessionSet* const sessions = composer.sessions;
    if (sessions == nullptr) {
        return;
    }
    // One row per pane (tab_rows.h): a split tab is a group of rows, so
    // no pane of it drops out of the list behind whichever one has the
    // focus. A tab of one pane is one row, as it always was.
    tabRows(*sessions, composer.bookmarks, collapsed, rows);
    heights.clear();
    for (const TabRow& row : rows) {
        heights.pushBack(row.label ? sidebarLabelRowHeight : sidebarRowHeight);
    }
    const NSUInteger count = (NSUInteger)(rows.length());
    NSMutableArray<NSString*>* const next = [NSMutableArray arrayWithCapacity:count];
    NSMutableArray<NSString*>* const nextFolders = [NSMutableArray arrayWithCapacity:count];
    NSMutableArray<NSString*>* const nextBranches = [NSMutableArray arrayWithCapacity:count];
    Buffer directory;
    Buffer branch;
    active = 0;
    StringBuilder status;
    for (size_t at = 0; at < rows.length(); ++at) {
        const TabRow& row = rows[at];
        if (!row.label && !row.closed && row.activeTab && row.focused) {
            active = at;
        }
        if (row.label) {
            // A folder's label: its name, in small capitals when drawn.
            [next addObject:[sidebarText(row.folder) uppercaseString]];
            [nextFolders addObject:@""];
            [nextBranches addObject:@""];
            continue;
        }
        // A bookmark's head row - the closed row, or the first row of the
        // tab opened from it - says the bookmark's name and whether it is
        // open, in place of a title and a folder: the name is what the
        // user gave it, and a status is what the variant on the canvas
        // shows there.
        const Bookmark* const bookmark = row.bookmark != 0 && composer.bookmarks != nullptr ? composer.bookmarks->find(row.bookmark) : nullptr;
        if (bookmark != nullptr && (!row.grouped || row.groupFirst)) {
            bookmarkStatus(*bookmark, rowState(at), status);
            [next addObject:sidebarText(bookmark->title)];
            [nextFolders addObject:sidebarText(StringView(status))];
            [nextBranches addObject:@""];
            continue;
        }
        // A pane whose shell never set a title shows the brand name,
        // like a fresh window does. The title goes on the row whole:
        // it is the line that says what is *running*, and cutting it
        // down to a path component would make it a second copy of the
        // folder line below it.
        // A name the user gave the tab labels its first row.
        StringView title = !row.grouped || row.groupFirst ? sessions->tabTitle(row.tab) : StringView();
        if (title.length() == 0) {
            title = sessions->paneTitle(row.pane);
        }
        if (title.length() == 0) {
            title = composer.brand->displayName();
        }
        [next addObject:sidebarText(title)];

        // The directory is the pane's shell process's own, asked of the
        // kernel (process_directory.cpp) rather than of the shell's
        // cooperation - OSC 7 never reaches this terminal at all.
        //
        // Read here rather than cached: this runs on a title change and
        // on any change to the set of tabs, which is exactly when a
        // directory or a branch can have moved, and no oftener. One
        // stat-and-read per row, measured at well under a tenth of a
        // millisecond.
        if (processDirectory(sessions->panePid(row.pane), directory)) {
            [nextFolders addObject:sidebarText(sidebarTabsShortTitle(StringView(directory)))];
            [nextBranches addObject:sidebarTabsBranch(StringView(directory), branch) ? sidebarText(StringView(branch)) : @"no git"];
        } else {
            // Nothing is known about this pane beyond its title - no
            // process to ask, or one this user may not inspect. Both
            // lines stay empty: "no git" here would be a claim about a
            // directory nobody has looked at.
            [nextFolders addObject:@""];
            [nextBranches addObject:@""];
        }
    }
    [next retain];
    [labels release];
    labels = next;
    [nextFolders retain];
    [folders release];
    folders = nextFolders;
    [nextBranches retain];
    [branches release];
    branches = nextBranches;
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
    // Before the early exit below: the layers stay when cmd+b puts the
    // list away - the panel widens over the surface, it does not vanish.
    applyLayers();
    if (peeking && !layered()) {
        peeking = false;
    }
    applyWindowButtons(window);
    applyEdge(window);
    if (!listed()) {
        if (view != nil) {
            [NSObject cancelPreviousPerformRequestsWithTarget:view];
            [view removeFromSuperview];
            [view release];
            view = nil;
        }
        dropPill();
        return;
    }
    const NSRect bounds = content.bounds;
    const CGFloat width = (CGFloat)(widthPoints());
    if (peeking) {
        // Over the terminal, in the frame view just above the content view
        // and so below the title bar's buttons: the content view's layer is
        // clipped to the panel's rounded corners, and a list inside it
        // would lose the corner it floats over.
        NSView* const frameView = content.superview;
        if (frameView == nil) {
            return;
        }
        const NSRect outer = frameView.bounds;
        const NSRect floating = NSMakeRect(NSMinX(outer), NSMinY(outer), width, outer.size.height);
        if (view == nil) {
            view = [[TerminalSidebarView alloc] initWithFrame:floating];
            view.wantsLayer = YES;
            view->owner = this;
        }
        if (view.superview != frameView) {
            [view removeFromSuperview];
            [frameView addSubview:view positioned:NSWindowAbove relativeTo:content];
        }
        view.autoresizingMask = NSViewMaxXMargin | NSViewHeightSizable;
        view.frame = floating;
        // A sheet of its own: the surface it docks on is under the panel,
        // and the terminal's text would show through a bare list.
        CALayer* const sheet = view.layer;
        sheet.backgroundColor = [surfaceColor() colorWithAlphaComponent:sidebarPeekOpacity].CGColor;
        sheet.cornerRadius = sidebarPeekRadius;
        // Only the free edge: the other three are the window's own.
        sheet.maskedCorners = kCALayerMaxXMinYCorner | kCALayerMaxXMaxYCorner;
        // No hairline: three of its edges are the window's, and the shadow
        // says where the fourth leaves the terminal.
        sheet.borderWidth = 0;
        sheet.masksToBounds = NO;
        sheet.shadowColor = NSColor.blackColor.CGColor;
        sheet.shadowOpacity = 0.55f;
        sheet.shadowRadius = 20;
        sheet.shadowOffset = CGSizeMake(6, 0);
        dropPill();
        view.needsDisplay = YES;
        return;
    }
    if (view != nil && view.superview != content) {
        // Back from peeking: docked again on the surface, bare.
        [view removeFromSuperview];
        [content addSubview:view];
        CALayer* const sheet = view.layer;
        sheet.backgroundColor = nil;
        sheet.cornerRadius = 0;
        sheet.borderWidth = 0;
        sheet.shadowOpacity = 0;
    }
    // -autoHideChrome puts NSWindowStyleMaskFullSizeContentView on the
    // window, so the content view runs up behind the title bar and the
    // top rows of a left-edge panel would sit under the traffic lights.
    // The strip ui_csd_tabs.mm already reserved is exactly how much to
    // stay clear of; without the option it is zero and nothing moves.
    // C10: the whole height of the content view, title bar included.
    //
    // It used to stop below the chrome reserve, which left a gap at the
    // top edge - the content view is not flipped, so a shortened frame
    // loses its top - and that gap is what a user reported as the panel
    // "not reaching the top of the window". The title bar is drawn over
    // the panel instead of pushing it down: the title bar container is a
    // sibling of the content view in the frame view and sits above it,
    // so this needs no ordering of its own and takes no clicks from it.
    //
    // The reserve itself is untouched, and that is the load-bearing
    // half. It is the terminal's - Composer::contentInsets() keeps the
    // grid out of it - and zeroing it would put the text under the title
    // bar. V2 was once told to zero it, checked, and refused; she was
    // right. What moves is this view's frame and the inset its own list
    // draws at, and nothing the grid can see.
    const CGFloat height = bounds.size.height;
    if (!(height > 0)) {
        return;
    }
    const NSRect frame = NSMakeRect(NSMinX(bounds), NSMinY(bounds), width, height);
    if (view == nil) {
        view = [[TerminalSidebarView alloc] initWithFrame:frame];
        // Pinned to the left edge and as tall as the content: the same
        // strip Composer::contentInsets() keeps the grid out of, so the
        // two never disagree about where the terminal begins. The
        // content view is not flipped, so leaving both vertical margins
        // fixed keeps the title-bar gap at the top where it belongs.
        view.autoresizingMask = NSViewMaxXMargin | NSViewHeightSizable;
        view.wantsLayer = YES;
        view->owner = this;
        [content addSubview:view];
        if (composer.vtConfig.config->verbose) {
            fprintf(stderr, "%s: sidebar: tab list installed on the left edge\n", composer.brand->identifierCString());
        }
    } else {
        view.frame = frame;
    }
    applyPill();
    view.needsDisplay = YES;
}

// The active row's floating pill of glass, when the window has glass under it.
//
// T4 gave the whole panel a sheet of its own, and that sheet is what this
// replaces. The user looked at the result and asked for the opposite: the
// sidebar and the terminal one surface, parted by a hairline, with only the
// active tab floating. A sheet over the strip is exactly what made the two
// different - the window's backdrop already runs under the whole window, and
// a second sheet over half of it is a second surface by construction. So the
// strip now paints nothing at all (drawRect: below) and shows the backdrop
// the grid shows, and the glass that is left is one row wide.
//
// Where it goes, and why not inside `view`. Subviews composite *above* their
// superview's drawRect:, so a pill parented to the panel would cover the row's
// own text. It goes beside the panel instead, inside the content view and
// below `view`, which is where T4's sheet went and for the same reasons: over
// the CAMetalLayer, which in this strip carries only the frame clear, and
// under everything the panel draws.
//
// Style Clear, not Regular, and measured rather than reasoned by analogy.
// Regular frosts what is behind it; the window's backdrop has already done
// that, and a second frosting only takes the colour out - on a probe over the
// composed backdrop the Regular pill came out 12.6 units *darker* than the
// surface it floats on, where Clear came out 66.7 lighter, with an edge of
// 103 against 18. Darker also inverts what the list means: every other shade
// here is foreground mixed into background, so the active row has always been
// the lighter one, and today's fill is +41.5 on the same scale.
//
// T6 kept the style and took back the brightness. Clear is still the only one
// of the two that reads as a sheet - Regular is 11.17:1 on the text and would
// have ended this argument, but it comes out 15.6 units *darker* than its
// surface with an edge of 18 against 103, which is a hole in the panel and not
// a floating tab. What was wrong was never the style; it was that untinted
// Clear hands the desktop straight through. tintColor is the knob for that.
//
// How far the tint is pulled is -sidebarTabTint, 0..100 on backgroundOpacity's
// scale, and T7 took it out of a constant because the look is a matter of
// taste over a desktop nobody here can see.
//
// The tint is opts->bg, not a grey and not a black, and that is what lets one
// number serve every theme: as the fraction rises the pill approaches the
// colour the terminal already draws its text on, so the limit of this knob is
// the theme's own contrast - 9.24:1 on the theme it was reported against -
// instead of some colour of ours that a light theme would invert.
//
// The default 65 is where T6 measured. Untinted, the active title stands
// against the pill at 3.48:1 over a dark desktop and 2.66:1 over a light one,
// both under the 4.5:1 ordinary text needs; 65 is the largest fraction that
// still leaves the pill *lighter* than the surface it floats on over both
// fields measured (+27.1 units dark, +15.0 light) and buys 6.38:1 and 5.72:1.
// 80 reads better still, 7.35 and 6.92, but over a light desktop it puts the
// pill 5.7 units *darker* than its surface, which turns the active row into a
// hole. Raising it past that is now the user's to make, and so is 0, which is
// exactly the untinted Clear T5 shipped.
//
// NSGlassEffectContainerView was tried in all three shapes it has - the pill
// alone as a direct subview, the pill and the window's backdrop together, and
// the documented form with a contentView - and it takes the pill away: 0.00,
// 0.05 and 0.76 units of difference against a shot with no pill at all, with
// edges of 0.0 to 1.7. T4 measured the same for a full-height sheet. Merged
// glass is the backdrop, and the backdrop already covers the window.
void SidebarTabsUi::applyPill() {
#if UI_SDK_MACOS_26
    if (@available(macOS 26.0, *)) {
        NSView* const content = view == nil ? nil : view.superview;
        // The active index can be past the end while a tab is closing, and a
        // pill for a row that is not in the list would be a bright rectangle
        // over nothing.
        // Not on the layered surface, whose selection is drawn flat
        // (drawRect:): glass on glass is what the user asked to lose there.
        if (content != nil && surface == nil && windowBackdropIsGlass(content.window) && active < (size_t)(labels.count)) {
            const NSRect where = sidebarPillFor(view.bounds, rowRect(view.bounds, active));
            if (!NSIsEmptyRect(where)) {
                // The panel is flipped and the content view is not, so the
                // rect has to be carried across rather than copied.
                const NSRect frame = [content convertRect:where fromView:view];
                // Named here rather than in the branch that builds it: the
                // field is an NSView*, so the file still compiles against an
                // SDK with no glass in it, and the tint below is a property
                // only the glass class has.
                NSGlassEffectView* sheet = nil;
                if (pill == nil) {
                    sheet = [[NSGlassEffectView alloc] initWithFrame:frame];
                    sheet.style = NSGlassEffectViewStyleClear;
                    // The pill's own radius, the one drawRect: draws the
                    // hovered row with: the two are the same shape and only
                    // one of them is glass.
                    //
                    // On macOS 27 this is the whole statement of the shape
                    // too. T9 made the pill a capsule there through
                    // cornerConfiguration - 25.5 points on a 51-point row -
                    // and the user looked at it beside Finder and asked for
                    // the squarer corner back (T10). A glass view given no
                    // configuration of its own resolves one from this number
                    // (T9 measured it: control tile, cornerRadius 6, reads
                    // back effective 6), which is what the verbose line below
                    // prints and how the choice is checked.
                    sheet.cornerRadius = sidebarPillRadius;
#if UI_SDK_MACOS_27
                    // The header asks for this on "glass that is used as the
                    // background for interactive controls", which a row a
                    // click selects is. Measured on a probe, on two fields a
                    // long way apart in brightness: at rest and under the
                    // pointer the frames are byte-identical with it on and
                    // off (max=0 of 255), and under a press the pill lifts by
                    // 10.3% of its luminance on a dark field and 7.4% on a
                    // light one. So there is no resting cost to weigh.
                    //
                    // It is inert as this view is parented today, and that is
                    // measured too: the response only appears where hit
                    // testing hands the glass the event itself, and this
                    // sheet sits below the panel, which takes every click in
                    // the list. A -mouseDown: forwarded to it by hand does
                    // not raise the response either (witnessed: the panel
                    // counted the forward, the pixels did not move). It is
                    // set because this is the view the header describes and
                    // because the day the parenting changes is not the day
                    // anyone will think to look for a flag.
                    if (@available(macOS 27.0, *)) {
                        sheet.effectIsInteractive = YES;
                    }
#endif
                    // A glass view ships this set NO, and a view in that state
                    // takes its frame from constraints nobody here writes -
                    // the same thing T3 measured on the backdrop.
                    sheet.translatesAutoresizingMaskIntoConstraints = YES;
                    // Fixed size, fixed distance from the top: rows are laid
                    // out from the top of the panel down, and the content view
                    // is not flipped, so a window growing taller must leave
                    // the pill where it is rather than stretch it or carry it
                    // down with the bottom edge.
                    sheet.autoresizingMask = NSViewMinYMargin;
                    pill = sheet;
                    [content addSubview:sheet positioned:NSWindowBelow relativeTo:view];
                    if (composer.vtConfig.config->verbose) {
                        // The resolved radii are printed, not the shape that
                        // was asked for: a corner configuration is a request
                        // the system answers, and the answer is the only
                        // thing worth reading back. Zero on macOS 26, where
                        // nothing resolves anything and cornerRadius is the
                        // whole story.
                        double resolved = 0;
#if UI_SDK_MACOS_27
                        if (@available(macOS 27.0, *)) {
                            NSViewCornerRadii* const radii = sheet.effectiveCornerRadii;
                            resolved = radii == nil ? 0 : (double)(radii.topLeft);
                        }
#endif
                        fprintf(stderr, "%s: sidebar: glass pill on the active tab, corner radius %.1f\n", composer.brand->identifierCString(), resolved);
                    }
                } else {
                    sheet = (NSGlassEffectView*)(pill);
                    sheet.frame = frame;
                }
                // Set on every pass, not only where the sheet is built, and
                // that is the half a reload can see: -sidebarTabTint and the
                // theme's own bg both come out of the fresh snapshot, and a
                // tint written once at construction would leave the pill on
                // the colour the window started with until it was closed.
                // Cheap enough to write unconditionally - a colour and a
                // setter, on a view that already exists.
                sheet.tintColor = nsColorFromTerminalColor(composer.vtConfig.config->bg, (CGFloat)(composer.opts->sidebarTabTint) / 100.0);
                return;
            }
        }
    }
#endif
    // No glass behind the window, or no row to stand on: whatever was
    // installed has to go, or a sheet stays where the list no longer has a
    // row and the fill drawRect: falls back to lands under it.
    dropPill();
}

void SidebarTabsUi::dropPill() {
    if (pill != nil) {
        [pill removeFromSuperview];
        [pill release];
        pill = nil;
    }
}

// The layered window: on for a Cocoa window with the sidebar chosen and
// -layeredWindow set, and only when the terminal's layer can be seen
// through. The last condition is not a nicety. Outside the panel the
// renderer clears to nothing, and a CAMetalLayer created opaque throws
// that alpha away - the surface would come out black. Asked of the live
// layer, as windowTintAlpha() asks it, because the window's transparency
// is settled once at creation and an option can have moved since.
bool SidebarTabsUi::layered() const {
    return layeredWindowShown(composer, nativeWindow());
}

// Where the list starts, below whatever sits over the top of the strip: the
// title bar's reserve as before, or on the layered surface the window's
// standard buttons, which are shown there for good.
//
// A window with no decorations has no buttons to clear: there the first row
// lines up with the panel's top edge instead, which is gap points down, less
// the air the list keeps above its first row anyway.
CGFloat SidebarTabsUi::listInset() const {
    if (surface == nil) {
        return (CGFloat)(composer.chromeReserve(ChromeSide::Top));
    }
    if (composer.opts->noDecorations) {
        return max<CGFloat>(0, (CGFloat)(composer.opts->panelGap) - sidebarListTop);
    }
    return sidebarLayeredListTop;
}

namespace {
    // The panel's rectangle in the content view's own coordinates, from
    // Composer::panelRect() - the one place that knows where it is - and
    // the scale the pixels were counted at. Measured from the bottom, the
    // way an unflipped view counts: the bottom gap stays the bottom gap
    // while a live resize is between two re-counts.
    NSRect panelRectIn(const Composer& composer) {
        const PixelRect panel = composer.panelRect();
        const CGFloat scale = composer.contentScale > 0 ? (CGFloat)(composer.contentScale) : 1.0;
        const CGFloat below = (CGFloat)(composer.geometry.pixelHeight) - (CGFloat)(panel.y) - (CGFloat)(panel.height);
        return NSMakeRect((CGFloat)(panel.x) / scale, below / scale, (CGFloat)(panel.width) / scale, (CGFloat)(panel.height) / scale);
    }

    // A rounded rectangle's radius as far as the rectangle allows:
    // CGPathAddRoundedRect asserts on one past half a side.
    CGFloat panelRadiusFor(NSRect rect, u16 radius) {
        const CGFloat most = min<CGFloat>(rect.size.width, rect.size.height) / 2;
        return max<CGFloat>(0, min<CGFloat>((CGFloat)(radius), most));
    }

    // Even-odd over `outer`, the panel, and the panel rounded: inside the
    // rounded panel the count is three, in the panel's corners outside the
    // curve it is two, everywhere else in `outer` it is one. So the same
    // path is both shapes this file needs - the surface with the panel cut
    // out of it, and a clip that keeps everything but the corners - by
    // whether the square is added at all.
    CGPathRef panelPath(NSRect outer, NSRect panel, CGFloat radius, bool keepPanel) {
        CGMutablePathRef path = CGPathCreateMutable();
        CGPathAddRect(path, nullptr, NSRectToCGRect(outer));
        if (keepPanel) {
            CGPathAddRect(path, nullptr, NSRectToCGRect(panel));
        }
        if (!NSIsEmptyRect(panel)) {
            CGPathAddRoundedRect(path, nullptr, NSRectToCGRect(panel), radius, radius);
        }
        return path;
    }
}

void SidebarTabsUi::applyLayers() {
    layersPending = false;
    NSWindow* const window = nativeWindow();
    NSView* const content = window == nil ? nil : window.contentView;
    NSView* const frameView = content == nil ? nil : content.superview;
    if (frameView == nil || !layered() || composer.geometry.pixelWidth == 0) {
        dropLayers();
        return;
    }
    const NSRect panel = panelRectIn(composer);
    const CGFloat radius = panelRadiusFor(panel, composer.opts->panelRadius);
    // No implicit animations: a path or a frame that eases into place
    // lags the terminal, which the renderer has already drawn where it
    // now belongs.
    [CATransaction begin];
    [CATransaction setDisableActions:YES];

    // The lower layer. The colour is -sidebarColor, or the shade the
    // sidebar has always defaulted to - six percent of the foreground in
    // the background - and the opacity is -sidebarOpacity. Not glass's
    // tintColor on the window's backdrop: that tints the whole window,
    // the part under the panel included, and the panel would wear the
    // surface's colour through its own.
    if (surface == nil) {
        surface = [[NSView alloc] initWithFrame:frameView.bounds];
        surface.wantsLayer = YES;
        surface.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [frameView addSubview:surface positioned:NSWindowBelow relativeTo:content];
        if (composer.vtConfig.config->verbose) {
            fprintf(stderr, "%s: sidebar: layered window, panel %.0fx%.0f pt\n", composer.brand->identifierCString(), (double)(panel.size.width), (double)(panel.size.height));
        }
    }
    surface.frame = frameView.bounds;
    NSColor* const foreground = nsColorFromTerminalColor(composer.vtConfig.config->fg);
    NSColor* const base = surfaceColor();
    surface.layer.backgroundColor = [base colorWithAlphaComponent:(CGFloat)(composer.opts->sidebarOpacity) / 100.0].CGColor;
    // The frame view's coordinates, which is where the surface lives; the
    // content view sits below the title bar unless the window runs its
    // content up behind it.
    const NSRect panelInFrame = [frameView convertRect:panel fromView:content];
    CAShapeLayer* hole = (CAShapeLayer*)(surface.layer.mask);
    if (hole == nil) {
        hole = [CAShapeLayer layer];
        hole.fillRule = kCAFillRuleEvenOdd;
        surface.layer.mask = hole;
    }
    hole.frame = surface.layer.bounds;
    CGPathRef holePath = panelPath(surface.bounds, panelInFrame, radius, false);
    hole.path = holePath;
    CGPathRelease(holePath);

    // The panel's shadow on the surface, and the hairline round its edge.
    // Both are sublayers of the surface, under the hole the panel is cut
    // out of it with: the mask clips a layer's sublayers and their shadows
    // too, so what the panel casts lands on the surface and never under the
    // panel, where a translucent terminal would show it through.
    CGPathRef rounded = CGPathCreateWithRoundedRect(NSRectToCGRect(panelInFrame), radius, radius, nullptr);
    if (shadow == nil) {
        shadow = [CALayer layer];
        shadow.shadowColor = NSColor.blackColor.CGColor;
        shadow.shadowOpacity = (float)(sidebarPanelShadowOpacity);
        shadow.shadowRadius = sidebarPanelShadowRadius;
        shadow.shadowOffset = CGSizeMake(sidebarPanelShadowOffset, 0);
        [surface.layer addSublayer:shadow];
    }
    shadow.frame = surface.layer.bounds;
    shadow.shadowPath = rounded;
    if (edge == nil) {
        edge = [CAShapeLayer layer];
        edge.fillColor = nil;
        // Twice the hairline: the hole takes the inner half.
        edge.lineWidth = 2;
        [surface.layer addSublayer:edge];
    }
    edge.frame = surface.layer.bounds;
    edge.path = rounded;
    edge.strokeColor = [foreground colorWithAlphaComponent:sidebarPanelEdgeAlpha].CGColor;
    CGPathRelease(rounded);

    // The upper layer's glass, where the backdrop is glass: a sheet the
    // size of the panel between the surface and the terminal. Untinted -
    // the renderer paints the panel's colour over it at -backgroundOpacity,
    // and a tint under that would be the colour twice.
#if UI_SDK_MACOS_26
    if (@available(macOS 26.0, *)) {
        if (panelGlass == nil && windowBackdropIsGlass(window)) {
            NSGlassEffectView* const glass = [[NSGlassEffectView alloc] initWithFrame:panelInFrame];
            glass.style = NSGlassEffectViewStyleRegular;
            // Honour the frame set below rather than constraints nobody
            // writes: the same trap the backdrop and the pill step around.
            glass.translatesAutoresizingMaskIntoConstraints = YES;
            glass.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
            panelGlass = glass;
            [frameView addSubview:glass positioned:NSWindowBelow relativeTo:content];
        }
        if (panelGlass != nil) {
            NSGlassEffectView* const glass = (NSGlassEffectView*)(panelGlass);
            glass.frame = panelInFrame;
            glass.cornerRadius = radius;
        }
    }
#endif

    // The panel's corners. The renderer paints a rectangle; this keeps
    // everything of the content view's layer except the four corners the
    // curve leaves outside it - the sidebar, the pill and anything else
    // parented to the content view lie outside the panel and stay whole.
    CALayer* const terminal = content.layer;
    if (terminal != nil) {
        if (clip == nil) {
            clip = [[CAShapeLayer alloc] init];
            clip.fillRule = kCAFillRuleEvenOdd;
        }
        clip.frame = terminal.bounds;
        CGPathRef clipPath = panelPath(content.bounds, panel, radius, true);
        clip.path = clipPath;
        CGPathRelease(clipPath);
        if (terminal.mask != clip) {
            terminal.mask = clip;
        }
    }

    // The panel's title bar: the top band of the panel, which Composer
    // keeps the grid out of (setPanelLayer's header). Pinned to the top and
    // as wide as the panel; the content view is not flipped, so the band is
    // measured down from the panel's upper edge.
    const CGFloat band = min<CGFloat>((CGFloat)(sidebarPanelHeader), panel.size.height);
    const NSRect headerFrame = NSMakeRect(NSMinX(panel), NSMaxY(panel) - band, panel.size.width, band);
    if (header == nil) {
        header = [[TerminalPanelHeaderView alloc] initWithFrame:headerFrame];
        header->owner = this;
        header.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
        [content addSubview:header];
    }
    header.frame = headerFrame;
    // Past the window's buttons when the panel runs under them (cmd+b put
    // the sidebar away), and just inside the panel when the sidebar holds
    // the buttons instead.
    // With no decorations there are no buttons to clear at all.
    // Hidden with the sidebar (applyWindowButtons), they need no room then.
    const CGFloat buttonsRight = composer.opts->noDecorations || !revealed ? 0 : sidebarWindowButtonsRight;
    header->leading = max<CGFloat>(8, buttonsRight - NSMinX(panel));
    header.needsDisplay = YES;
    [CATransaction commit];
}

NSColor* SidebarTabsUi::surfaceColor() const {
    NSColor* const background = nsColorFromTerminalColor(composer.vtConfig.config->bg);
    NSColor* const foreground = nsColorFromTerminalColor(composer.vtConfig.config->fg);
    return composer.opts->sidebarColorSet
        ? nsColorFromTerminalColor(composer.opts->sidebarColor)
        : sidebarMix(background, foreground, sidebarPanelTint);
}

void SidebarTabsUi::applyWindowButtons(NSWindow* window) {
    if (window == nil || composer.opts->noDecorations) {
        return;
    }
    // The buttons live on the sidebar: put away with it, back with it when
    // it docks or comes out at the edge.
    const BOOL hide = layered() && composer.opts->sidebarTabs && !revealed && !peeking;
    const NSWindowButton kinds[3] = {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton};
    for (const NSWindowButton kind : kinds) {
        NSButton* const button = [window standardWindowButton:kind];
        if (button != nil && button.hidden != hide) {
            button.hidden = hide;
        }
    }
}

void SidebarTabsUi::applyEdge(NSWindow* window) {
    NSView* const content = window == nil ? nil : window.contentView;
    NSView* const frameView = content == nil ? nil : content.superview;
    const bool wanted = frameView != nil && layered() && composer.opts->sidebarTabs && !revealed;
    if (!wanted) {
        if (edgeZone != nil) {
            [edgeZone removeFromSuperview];
            [edgeZone release];
            edgeZone = nil;
        }
        return;
    }
    const NSRect outer = frameView.bounds;
    const NSRect strip = NSMakeRect(NSMinX(outer), NSMinY(outer), sidebarPeekZone, outer.size.height);
    if (edgeZone == nil) {
        edgeZone = [[TerminalEdgeView alloc] initWithFrame:strip];
        edgeZone->owner = this;
        edgeZone.autoresizingMask = NSViewMaxXMargin | NSViewHeightSizable;
        [frameView addSubview:edgeZone positioned:NSWindowAbove relativeTo:content];
    }
    edgeZone.frame = strip;
}

void SidebarTabsUi::peek() {
    if (!composer.opts->sidebarTabs || revealed || peeking || !layered()) {
        return;
    }
    peeking = true;
    apply();
    if (view != nil) {
        [view performSelector:@selector(peekCheck) withObject:nil afterDelay:sidebarPeekDelay];
    }
}

void SidebarTabsUi::peekCheck() {
    NSWindow* const window = nativeWindow();
    if (!peeking || view == nil || window == nil) {
        return;
    }
    // Asked of the pointer rather than told by the view's own exit: the
    // pointer that brought the list out is on the edge strip, not on the
    // list, and may never enter it at all.
    const NSPoint inWindow = [window convertPointFromScreen:NSEvent.mouseLocation];
    NSView* const frameView = view.superview;
    const NSPoint point = frameView != nil ? [frameView convertPoint:inWindow fromView:nil] : inWindow;
    const bool onList = NSPointInRect(point, NSInsetRect(view.frame, -sidebarPeekSlack, -sidebarPeekSlack));
    const bool onEdge = point.x < sidebarPeekZone;
    if (onList || onEdge || [view popoverShown] || window.attachedSheet != nil) {
        [view performSelector:@selector(peekCheck) withObject:nil afterDelay:sidebarPeekDelay];
        return;
    }
    endPeek();
}

void SidebarTabsUi::endPeek() {
    if (!peeking) {
        return;
    }
    peeking = false;
    apply();
}

void SidebarTabsUi::endPeekSoon() {
    // Not from inside the list's own event handler: taking the list away
    // there would free the view whose method is still running.
    if (!peeking) {
        return;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        endPeek();
    });
}

void SidebarTabsUi::dropLayers() {
    layersPending = false;
    if (header != nil) {
        [header removeFromSuperview];
        [header release];
        header = nil;
    }
    // Sublayers of the surface: they leave with it.
    shadow = nil;
    edge = nil;
    if (surface != nil) {
        [surface removeFromSuperview];
        [surface release];
        surface = nil;
    }
    if (panelGlass != nil) {
        [panelGlass removeFromSuperview];
        [panelGlass release];
        panelGlass = nil;
    }
    if (clip != nil) {
        NSWindow* const window = nativeWindow();
        CALayer* const terminal = window == nil ? nil : window.contentView.layer;
        if (terminal != nil && terminal.mask == clip) {
            terminal.mask = nil;
        }
        [clip release];
        clip = nil;
    }
}

void SidebarTabsUi::toggle() {
    if (!composer.opts->sidebarTabs) {
        // The chord is bound whether or not the option is: without the
        // panel there is nothing to show or hide, and swallowing cmd+b
        // to do nothing visible would be worse than passing it on.
        return;
    }
    revealed = !revealed;
    // The toggle pressed while the list is out pins it: the second press
    // is the ordinary cmd+b again.
    peeking = false;
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

void SidebarTabsUi::rowSelected(size_t row) {
    SessionSet* const sessions = composer.sessions;
    if (sessions == nullptr || row >= rows.length()) {
        return;
    }
    // A bookmark with no tab opens one; the tab it has is just a tab.
    if (rows[row].closed) {
        const Bookmark* const bookmark = composer.bookmarks != nullptr ? composer.bookmarks->find(rows[row].bookmark) : nullptr;
        if (bookmark != nullptr) {
            sessions->openBookmark(*bookmark);
            composer.window->requestFrame();
        }
        endPeekSoon();
        return;
    }
    // The row's pane, and with it its tab: a click on the second pane of
    // a background split brings that tab forward with that pane focused.
    sessions->activatePane(rows[row].pane);
    // A kept pane whose child has exited: the click that brings it forward
    // is also the one that runs it again, as its status line says.
    if (rows[row].exited) {
        sessions->reconnect(rows[row].pane);
    }
    composer.window->requestFrame();
    endPeekSoon();
}

BookmarkState SidebarTabsUi::rowState(size_t row) const {
    if (row >= rows.length()) {
        return BookmarkState::Closed;
    }
    const TabRow& model = rows[row];
    const bool idle = model.closed || model.exited;
    if (idle && composer.bookmarkProbe != nullptr && composer.bookmarkProbe->unreachable(model.bookmark)) {
        return BookmarkState::Unreachable;
    }
    return model.closed ? BookmarkState::Closed : model.exited ? BookmarkState::Exited : BookmarkState::Open;
}

bool SidebarTabsUi::rowPinnable(size_t row) const {
    if (row >= rows.length()) {
        return false;
    }
    const TabRow& model = rows[row];
    return !model.label && (!model.grouped || model.groupFirst);
}

void SidebarTabsUi::rowPinned(size_t row) {
    SessionSet* const sessions = composer.sessions;
    BookmarkShelf* const shelf = composer.bookmarks;
    if (sessions == nullptr || shelf == nullptr || !rowPinnable(row)) {
        return;
    }
    const TabRow model = rows[row];
    if (model.bookmark != 0 && shelf->find(model.bookmark) != nullptr) {
        // Unpinned, an open bookmark's tab stays open as an ordinary tab.
        if (unpinBookmark(*shelf, *composer.pool, composer.brand->identifier(), model.bookmark) && !model.closed) {
            sessions->adoptBookmark(model.tab, 0);
        }
    } else {
        Bookmark draft;
        tabBookmarkDraft(*sessions, model.tab, composer.brand->displayName(), *composer.pool, draft);
        u64 id = 0;
        if (pinBookmark(*shelf, *composer.pool, composer.brand->identifier(), draft, id)) {
            sessions->adoptBookmark(model.tab, id);
        }
    }
    if (composer.bookmarkProbe != nullptr) {
        composer.bookmarkProbe->watch(*shelf);
    }
    // adoptBookmark() has published when a tab moved; a closed bookmark
    // leaving the shelf moves no tab, and the list still has to follow.
    project();
    composer.window->requestFrame();
}

void SidebarTabsUi::folderToggled(size_t row) {
    if (row >= rows.length() || !rows[row].label) {
        return;
    }
    const StringView folder = rows[row].folder;
    bool wasShut = false;
    Vector<StringView> kept;
    for (const StringView name : collapsed) {
        if (name == folder) {
            wasShut = true;
        } else {
            kept.pushBack(name);
        }
    }
    if (!wasShut) {
        kept.pushBack(composer.pool->intern(folder));
    }
    collapsed.clear();
    for (const StringView name : kept) {
        collapsed.pushBack(name);
    }
    project();
}

StringView SidebarTabsUi::folderCreated() {
    SessionSet* const sessions = composer.sessions;
    if (sessions == nullptr) {
        return StringView();
    }
    // "New Folder", then "New Folder 2" and on: never one already there.
    Vector<StringView> order;
    sessions->folders(order);
    StringBuilder name;
    for (unsigned n = 1;; ++n) {
        name.reset();
        name << StringView(u8"New Folder");
        if (n > 1) {
            name << StringView(u8" ") << (i64)(n);
        }
        if (folderIndex(order, StringView(name)) == order.length()) {
            break;
        }
    }
    const StringView folder = composer.pool->intern(StringView(name));
    sessions->addFolder(folder);
    project();
    beginRename(folder);
    return folder;
}

void SidebarTabsUi::folderToggledByName(StringView folder) {
    for (size_t at = 0; at < rows.length(); ++at) {
        if (rows[at].label && rows[at].folder == folder) {
            folderToggled(at);
            return;
        }
    }
}

void SidebarTabsUi::rowClosed(size_t row) {
    SessionSet* const sessions = composer.sessions;
    if (sessions == nullptr || row >= rows.length() || rows[row].label || rows[row].closed) {
        return;
    }
    if (!sessions->close(rows[row].tab)) {
        composer.window->requestClose();
        return;
    }
    composer.window->requestFrame();
}

void SidebarTabsUi::folderIconChosen(StringView folder, StringView icon) {
    // A look is saved: the folder gets a [[folder]] table in the file, and
    // with it a place in the list even while nothing is in it.
    if (composer.bookmarks != nullptr) {
        setFolderIcon(*composer.bookmarks, *composer.pool, composer.brand->identifier(), folder, icon);
    }
    project();
}

void SidebarTabsUi::folderMembers(StringView folder, Vector<TabRow>& out) const {
    out.clear();
    SessionSet* const sessions = composer.sessions;
    if (sessions == nullptr) {
        return;
    }
    Vector<StringView> open;
    for (const StringView name : collapsed) {
        if (name != folder) {
            open.pushBack(name);
        }
    }
    Vector<TabRow> all;
    tabRows(*sessions, composer.bookmarks, open, all);
    for (const TabRow& row : all) {
        if (!row.label && row.folder == folder && (!row.grouped || row.groupFirst)) {
            out.pushBack(row);
        }
    }
}

NSString* SidebarTabsUi::rowTitle(const TabRow& row) const {
    const Bookmark* const bookmark = row.bookmark != 0 && composer.bookmarks != nullptr ? composer.bookmarks->find(row.bookmark) : nullptr;
    if (bookmark != nullptr) {
        return sidebarText(bookmark->title);
    }
    SessionSet* const sessions = composer.sessions;
    StringView title = sessions != nullptr ? sessions->tabTitle(row.tab) : StringView();
    if (title.length() == 0 && sessions != nullptr) {
        title = sessions->paneTitle(row.pane);
    }
    return sidebarText(title.length() != 0 ? title : composer.brand->displayName());
}

void SidebarTabsUi::beginRename(StringView folder) {
    NSWindow* const window = view != nil ? view.window : nil;
    if (window == nil || folder.empty()) {
        return;
    }
    const StringView kept = composer.pool->intern(folder);
    sidebarAskName(window, @"Rename Folder", sidebarText(kept), ^(NSString* text) {
        renaming = kept;
        commitRename(text);
    });
}

void SidebarTabsUi::beginRenameTab(size_t row) {
    NSWindow* const window = view != nil ? view.window : nil;
    SessionSet* const sessions = composer.sessions;
    if (window == nil || sessions == nullptr || row >= rows.length() || rows[row].label) {
        return;
    }
    // Remembered by what it is, not by its row: the list may redraw while
    // the sheet is up.
    const u64 bookmark = rows[row].bookmark;
    const u64 pane = rows[row].pane;
    const bool closed = rows[row].closed;
    sidebarAskName(window, bookmark != 0 ? @"Rename Bookmark" : @"Rename Tab", rowTitle(rows[row]), ^(NSString* text) {
        NSString* const trimmed = [text stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
        const StringView name(trimmed.UTF8String != nullptr ? trimmed.UTF8String : "");
        BookmarkShelf* const shelf = composer.bookmarks;
        if (bookmark != 0 && shelf != nullptr && shelf->find(bookmark) != nullptr) {
            // A bookmark's name is saved: it is its title in the file.
            if (!name.empty()) {
                setBookmarkTitle(*shelf, *composer.pool, bookmark, name);
            }
        } else if (!closed) {
            Vector<u64> panes;
            for (size_t tab = 0; tab < sessions->count(); ++tab) {
                sessions->panes(tab, panes);
                for (const u64 candidate : panes) {
                    if (candidate == pane) {
                        sessions->setTabTitle(tab, name);
                    }
                }
            }
        }
        project();
    });
}

void SidebarTabsUi::beginDeleteFolder(StringView folder) {
    NSWindow* const window = view != nil ? view.window : nil;
    if (window == nil || folder.empty()) {
        return;
    }
    const StringView kept = composer.pool->intern(folder);
    Vector<TabRow> members;
    folderMembers(kept, members);
    size_t tabs = 0;
    size_t bookmarks = 0;
    for (const TabRow& row : members) {
        if (!row.closed) {
            ++tabs;
        }
        if (row.bookmark != 0 && composer.bookmarks != nullptr && composer.bookmarks->find(row.bookmark) != nullptr) {
            ++bookmarks;
        }
    }
    if (tabs == 0 && bookmarks == 0) {
        commitDeleteFolder(kept, false);
        return;
    }
    auto counted = [](size_t count, NSString* one, NSString* many) -> NSString* {
        return [NSString stringWithFormat:@"%zu %@", count, count == 1 ? one : many];
    };
    NSString* held = nil;
    if (tabs != 0 && bookmarks != 0) {
        held = [NSString stringWithFormat:@"%@ and %@", counted(tabs, @"open tab", @"open tabs"), counted(bookmarks, @"bookmark", @"bookmarks")];
    } else if (tabs != 0) {
        held = counted(tabs, @"open tab", @"open tabs");
    } else {
        held = counted(bookmarks, @"bookmark", @"bookmarks");
    }
    NSAlert* const alert = [[[NSAlert alloc] init] autorelease];
    alert.messageText = [NSString stringWithFormat:@"Delete folder “%@”?", sidebarText(kept)];
    alert.informativeText = [NSString stringWithFormat:@"It holds %@. Ungroup keeps them, out of any folder. Close Tabs closes its tabs%@.",
                                                       held, bookmarks != 0 ? @" and removes its bookmarks from bookmarks.toml" : @""];
    [alert addButtonWithTitle:@"Ungroup"];
    [alert addButtonWithTitle:@"Close Tabs"];
    [alert addButtonWithTitle:@"Cancel"];
    [alert beginSheetModalForWindow:window completionHandler:^(NSModalResponse response) {
        if (response == NSAlertFirstButtonReturn) {
            commitDeleteFolder(kept, false);
        } else if (response == NSAlertSecondButtonReturn) {
            commitDeleteFolder(kept, true);
        }
    }];
}

void SidebarTabsUi::commitDeleteFolder(StringView folder, bool closeTabs) {
    SessionSet* const sessions = composer.sessions;
    if (sessions == nullptr || folder.empty()) {
        return;
    }
    // Its tabs first, while tabFolder() still names it for a bookmark tab;
    // from the back, since a close moves the tabs behind it.
    bool last = false;
    if (closeTabs) {
        for (size_t tab = sessions->count(); tab-- > 0;) {
            if (tab < sessions->count() && sessions->tabFolder(tab) == folder && !sessions->close(tab)) {
                last = true;
                break;
            }
        }
    }
    if (composer.bookmarks != nullptr) {
        deleteFolderInFile(*composer.bookmarks, *composer.pool, composer.brand->identifier(), folder, closeTabs);
    }
    size_t shut = 0;
    for (size_t at = 0; at < collapsed.length(); ++at) {
        if (collapsed[at] != folder) {
            collapsed.mut(shut++) = collapsed[at];
        }
    }
    while (collapsed.length() > shut) {
        collapsed.popBack();
    }
    sessions->removeFolder(folder);
    project();
    if (last) {
        // The window's last tab was in it: the window goes, as Close Tab
        // on that tab would have it.
        composer.window->requestClose();
        return;
    }
    composer.window->requestFrame();
}

void SidebarTabsUi::commitRename(NSString* text) {
    SessionSet* const sessions = composer.sessions;
    if (renaming.empty() || sessions == nullptr) {
        return;
    }
    const StringView from = renaming;
    renaming = StringView();
    NSString* const trimmed = [text stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    const char* const utf8 = trimmed.UTF8String;
    const StringView to = composer.pool->intern(StringView(utf8 != nullptr ? utf8 : ""));
    if (!to.empty() && to != from) {
        // The bookmarks naming it move in their file, the window's tabs in
        // the model; a shut folder stays shut under its new name.
        BookmarkShelf* const shelf = composer.bookmarks;
        if (shelf != nullptr) {
            renameFolderInFile(*shelf, *composer.pool, composer.brand->identifier(), from, to);
        }
        for (size_t at = 0; at < collapsed.length(); ++at) {
            if (collapsed[at] == from) {
                collapsed.mut(at) = to;
            }
        }
        sessions->renameFolder(from, to);
    }
    project();
}

void SidebarTabsUi::rowDropped(size_t row, StringView folder, size_t before) {
    SessionSet* const sessions = composer.sessions;
    if (sessions == nullptr || row >= rows.length() || rows[row].label) {
        return;
    }
    const TabRow model = rows[row];
    BookmarkShelf* const shelf = composer.bookmarks;
    const Bookmark* const bookmark = model.bookmark != 0 && shelf != nullptr ? shelf->find(model.bookmark) : nullptr;
    if (bookmark != nullptr) {
        // A bookmark is moved in its file; its tab, if open, follows.
        if (bookmark->folder != folder) {
            setBookmarkFolder(*shelf, *composer.pool, composer.brand->identifier(), model.bookmark, folder);
            sessions->resort();
        }
    } else if (!model.closed) {
        sessions->dropTab(model.tab, folder, before);
    }
    project();
    composer.window->requestFrame();
}

void SidebarTabsUi::tabOpened() {
    SessionSet* const sessions = composer.sessions;
    if (sessions == nullptr) {
        return;
    }
    sessions->newSession();
    composer.window->requestFrame();
    endPeekSoon();
}

@implementation TerminalSidebarView

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
    // Every shade here is opts->fg mixed into opts->bg, rather than a
    // system label tier, for the reason spelled out at length in
    // ui_csd_tabs.mm: the tiers are tuned for the standard material and
    // know nothing about opts->bg, and a light system theme over a dark
    // terminal background made them effectively invisible (issue 84).
    // The panel itself is a shade off the terminal's background, which
    // is what makes it read as a panel rather than as grid with no text
    // in it - the iTerm2 and Ghostty treatment.
    NSColor* const background = nsColorFromTerminalColor(owner->composer.vtConfig.config->bg);
    NSColor* const foreground = nsColorFromTerminalColor(owner->composer.vtConfig.config->fg);
    NSColor* const accent = nsColorFromTerminalColor(owner->composer.vtConfig.config->cr);
    // C10. -sidebarColor sets the panel, and every other shade is mixed
    // from it rather than from the terminal's background: a panel whose
    // background is chosen by hand and whose active row is still derived
    // from opts->bg can drift until neither reads against the other.
    //
    // The fractions are re-based rather than reused. All seven shades
    // are points on one ray, and the panel sits at 0.06 along it; moving
    // the ray's origin to the panel means a shade that was at k is now
    // at (k - 0.06) / 0.94. At the default colour that is an identity -
    // mix(panel, fg, 0.2553) is mix(bg, fg, 0.30) - which is why one
    // formula serves both and there is no second set of constants to
    // keep in step.
    //
    // The unset branch is nevertheless left literally as it was. The
    // identity above is exact in real arithmetic and NSColor blends in
    // floats; a default that came out a byte different would still be a
    // default that changed, on a fork whose upstream has none of this.
    const bool ownColour = owner->composer.opts->sidebarColorSet;
    NSColor* const panel = ownColour
        ? nsColorFromTerminalColor(owner->composer.opts->sidebarColor)
        : sidebarMix(background, foreground, sidebarPanelTint);
    const auto shade = [&](CGFloat fraction) {
        return ownColour
            ? sidebarMix(panel, foreground, (fraction - sidebarPanelTint) / (1.0 - sidebarPanelTint))
            : sidebarMix(background, foreground, fraction);
    };
    NSColor* const separator = shade(0.30);
    NSColor* const activeFill = shade(0.20);
    NSColor* const hoverFill = shade(0.12);
    NSColor* const idleText = shade(0.62);
    NSColor* const dimText = shade(0.42);

    // S10. Two ways to paint the same panel, and which one is right
    // depends on what is already underneath.
    //
    // This view is a subview of the content view, and the content view's
    // layer *is* the CAMetalLayer - so the renderer has already painted
    // this strip. It is chrome reserve, outside every pane's rectangle,
    // which means what lies here is the frame clear: opts->bg, at
    // exactly the terminal's own alpha.
    //
    // So a translucent fill would not make the panel match the terminal,
    // it would stack on top of it: alpha a over alpha a composites to
    // 2a - a², which is 0.75 where the body is 0.5. The panel would read
    // as noticeably more solid than the window it belongs to - the same
    // complaint that brought this change, only quieter.
    //
    // So the panel is painted as the thinnest coat that still lands on
    // its colour over that backdrop (tint_coat.h). C10 generalised what
    // S10 did by hand: the default panel is six percent of the
    // foreground, and asked for that colour over that background the
    // coat comes out at about six percent of roughly the foreground -
    // the same paint, arrived at by a rule that also serves a colour
    // nobody could have guessed. On this project's own theme it is alpha
    // 14/255 where the hand-written tint was 15/255, and both land on
    // the same visible byte; what a chosen -sidebarColor gets is a coat
    // as thin as that colour allows, and a colour far from the
    // background allows only a thick one.
    //
    // The opaque branch stays a solid fill rather than being folded into
    // the same call, and not for the colour, which is identical: until
    // the renderer has painted, a live resize outruns it, and a solid
    // fill still shows a panel where a coat would show a few percent of
    // nothing. That is the reason the title bar keeps its own fill too.
    //
    // T4 added a third case above both and keyed it on its own sheet of
    // glass; T5 keeps the case and drops the sheet, so the key is now the
    // window's backdrop directly. Under glass the coat above has nothing to
    // land on: it exists to reach the panel's colour over a *known*
    // backdrop, and glass is not one - it refracts whatever is behind the
    // window. T5 therefore painted nothing here and parted the strip from
    // the grid with a hairline.
    //
    // T10 paints a tone instead, and no hairline. The user set the window
    // beside Finder on macOS 27 and asked for what Finder does: the sidebar
    // a tone apart from its content across its whole width, no line, the
    // edge a fade. The tone is opts->fg at sidebarGlassTone - toward fg
    // rather than lighter, so one rule serves both themes (lighter on a
    // dark theme, greyer on a light one).
    //
    // Painted here, which puts it OVER the pill of glass (applyPill parents
    // the pill below this view), and that is by measurement rather than by
    // the task's first instinct, which was to slide it under. Under, the
    // Clear pill samples the tone through its own bg tint and keeps only
    // about a third of it while the strip around it takes all of it, and
    // the pill drops 7 units nearer the strip on a dark field than it
    // stands today - the hole T6 refused. Painted over, the coat lands on
    // strip and pill alike and their difference does not move (probe,
    // both fields). The text is drawn after it and stays on top.
    //
    // The same colour at both ends of the fade, alpha aside: a ramp to a
    // clear black would pass through a darker grey on its way down.
    //
    // The fade is gone since: the user asked for no gradient on the
    // sidebar, so under glass the tone is flat across the whole strip.
    //
    // And the layered window paints nothing at all. The strip is part of
    // the surface there, which has its own view and its own colour below
    // the terminal (applyLayers); a coat here would be a second surface
    // over the first, and the panel's edge already says where the
    // terminal begins.
    const CGFloat tint = windowTintAlpha(owner->composer, self.window);
    const bool glassSurface = windowBackdropIsGlass(self.window);
    const bool layeredSurface = owner->surface != nil;
    // -sidebarTabColor, or fg when unset; -sidebarTabOpacity for the active
    // row, half of it under the pointer. The edge keeps the mock's 10% of the
    // same ink whatever the fill is: it is a hairline, not a second fill.
    NSColor* const tabInk = owner->composer.opts->sidebarTabColorSet
        ? nsColorFromTerminalColor(owner->composer.opts->sidebarTabColor)
        : foreground;
    const CGFloat tabAlpha = (CGFloat)(owner->composer.opts->sidebarTabOpacity) / 100.0;
    NSColor* const layeredActiveFill = [tabInk colorWithAlphaComponent:tabAlpha];
    NSColor* const layeredHoverFill = [tabInk colorWithAlphaComponent:tabAlpha / 2];
    NSColor* const layeredActiveEdge = [tabInk colorWithAlphaComponent:0.10];
    if (layeredSurface) {
        // Nothing: the surface underneath is the panel.
    } else if (glassSurface) {
        [nsColorFromTerminalColor(owner->composer.vtConfig.config->fg, sidebarGlassTone) setFill];
        NSRectFillUsingOperation(bounds, NSCompositingOperationSourceOver);
    } else if (tint >= 1.0) {
        [panel setFill];
        NSRectFill(bounds);
    } else {
        const TintCoat coat = thinnestCoat(terminalColorFromNsColor(panel), owner->composer.vtConfig.config->bg);
        [nsColorFromTerminalColor(coat.color, coat.alpha / 255.0) setFill];
        // Named rather than left to the default, though here the two
        // agree: this view is layer-backed and non-opaque, so its
        // backing store starts each drawRect: empty and the first fill
        // lands on nothing either way. The composite that does the work
        // is Core Animation's, of this layer over the metal one, and it
        // is always over. Saying `over` here puts that intent on the
        // page instead of leaving it to be re-derived.
        NSRectFillUsingOperation(bounds, NSCompositingOperationSourceOver);
    }
    // C10: where the list begins, which is below whatever the title bar
    // reserved. The same number sidebarTabsRowAt() is handed below, so
    // what is drawn here and what a click resolves to cannot part.
    const CGFloat listInset = owner->listInset();

    // The seam with the grid, on the trailing edge now that the panel is
    // on the left. Visible on purpose: a hairline this close to the
    // background was the "where does the terminal start" complaint.
    //
    // Not under glass, and not a fainter one there either: none. T6 drew a
    // black hairline here over glass; the user then looked at the window
    // beside Finder and asked for what Finder does instead - a strip a tone
    // apart from its content, meeting it in a fade (T10). Where the terminal
    // begins is now said by the tone and its edge, above, and a line on top
    // of a fade would be the old seam with a gradient behind it. blur and
    // off keep the pixel they have: there the strip paints, the surface is
    // known, and shade(0.30) was picked against it on purpose (C10).
    if (!glassSurface && !layeredSurface) {
        [separator setFill];
        NSRectFill(NSMakeRect(NSMaxX(bounds) - 1, NSMinY(bounds), 1, bounds.size.height));
    }

    // The title line is whatever the shell set, which is a command at the
    // head and often a path at the tail; both ends carry meaning, so it
    // loses the middle. The folder and branch lines are single names and
    // read from the head.
    NSMutableParagraphStyle* const titleStyle = [[[NSMutableParagraphStyle alloc] init] autorelease];
    titleStyle.lineBreakMode = NSLineBreakByTruncatingMiddle;
    NSMutableParagraphStyle* const style = [[[NSMutableParagraphStyle alloc] init] autorelease];
    style.lineBreakMode = NSLineBreakByTruncatingTail;
    const CGFloat fontSize = [NSFont smallSystemFontSize];
    NSFont* const font = [NSFont systemFontOfSize:fontSize];
    // Weight as well as color: the active row has to stay obvious on a
    // theme where every mix of fg into bg is subtle.
    NSFont* const activeFont = [NSFont systemFontOfSize:fontSize weight:NSFontWeightSemibold];
    NSFont* const subFont = [NSFont systemFontOfSize:fontSize - 1];
    NSDictionary* const activeAttributes = @{
        NSFontAttributeName: activeFont,
        NSForegroundColorAttributeName: foreground,
        NSParagraphStyleAttributeName: titleStyle,
    };
    NSDictionary* const idleAttributes = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: idleText,
        NSParagraphStyleAttributeName: titleStyle,
    };
    // The folder and the branch are context, not the label: a step
    // further toward the background than even an idle title, so the eye
    // reads down the titles first and only then across a row.
    NSDictionary* const subAttributes = @{
        NSFontAttributeName: subFont,
        NSForegroundColorAttributeName: dimText,
        NSParagraphStyleAttributeName: style,
    };
    NSDictionary* const closedAttributes = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: dimText,
        NSParagraphStyleAttributeName: titleStyle,
    };
    NSDictionary* const numberAttributes = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: dimText,
    };

    // The icon faces, resolved once per repaint rather than per row: the
    // terminal's font list is walked in order and the first face
    // carrying the glyph wins, exactly as the grid resolves a fallback.
    NSFont* folderIcon = nil;
    NSFont* branchIcon = nil;
    const Vector<StringView>& fontnames = owner->composer.opts->fontnames;
    for (size_t at = 0; at < fontnames.length() && (folderIcon == nil || branchIcon == nil); ++at) {
        if (folderIcon == nil) {
            folderIcon = sidebarFontCovering(fontnames[at], sidebarFolderIcon, fontSize + 2);
        }
        if (branchIcon == nil) {
            branchIcon = sidebarFontCovering(fontnames[at], sidebarBranchIcon, fontSize + 2);
        }
    }
    // All or nothing. One icon without the other would put the folder and
    // branch lines on different left edges, and a row whose two context
    // lines do not line up looks broken in a way a missing icon does not.
    const bool iconsAvailable = folderIcon != nil && branchIcon != nil;

    NSArray<NSString*>* const labels = owner->labels;
    const NSUInteger count = labels.count;
    const NSUInteger active = (NSUInteger)(owner->active);
    const CGFloat textLeft = NSMinX(bounds) + sidebarTextInset + sidebarNumberGutter;
    const CGFloat textRight = NSMaxX(bounds) - sidebarPillInset - 8;

    // Split groups: one frame round the rows of each split tab, drawn
    // under them so the selection and the hover still sit on top. It
    // encloses the pills with a little air rather than running edge to
    // edge, and stops at the last row that is drawn whole.
    NSColor* const groupFill = layeredSurface ? [tabInk colorWithAlphaComponent:tabAlpha / 3] : shade(0.10);
    NSColor* const groupEdge = layeredSurface ? [tabInk colorWithAlphaComponent:0.12] : shade(0.22);
    const CGFloat groupRadius = (layeredSurface ? sidebarLayeredPillRadius : sidebarPillRadius) + sidebarGroupInset;
    const Vector<TabRow>& rowModels = owner->rows;
    for (size_t first = 0; first < rowModels.length() && first < (size_t)(count); ++first) {
        if (!rowModels[first].groupFirst) {
            continue;
        }
        NSRect frame = NSZeroRect;
        for (size_t at = first; at < rowModels.length() && at < (size_t)(count); ++at) {
            const NSRect pill = sidebarPillFor(bounds, owner->rowRect(bounds, at));
            if (NSIsEmptyRect(pill)) {
                break;
            }
            frame = NSIsEmptyRect(frame) ? pill : NSUnionRect(frame, pill);
            if (rowModels[at].groupLast) {
                break;
            }
        }
        if (NSIsEmptyRect(frame)) {
            continue;
        }
        NSBezierPath* const shape = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(frame, -sidebarGroupInset + 0.5, -sidebarGroupInset + 0.5) xRadius:groupRadius yRadius:groupRadius];
        [groupFill setFill];
        [shape fill];
        [groupEdge setStroke];
        shape.lineWidth = 1;
        [shape stroke];

        // The group's map, in the frame's top-right corner: the split drawn
        // small, one cell per pane where the pane sits in the tab
        // (TabRow::left/top/width/height), the focused one lit - brightest
        // in the tab on screen. It is what says "this is a split" and which
        // part of it the keys are in, the variant the user chose on the
        // canvas.
        const NSRect map = NSMakeRect(NSMaxX(frame) - sidebarGroupMapRight - sidebarGroupMapWidth, NSMinY(frame) + sidebarGroupMapTop, sidebarGroupMapWidth, sidebarGroupMapHeight);
        for (size_t at = first; at < rowModels.length() && rowModels[at].tab == rowModels[first].tab; ++at) {
            const TabRow& cellRow = rowModels[at];
            const NSRect cell = NSMakeRect(
                NSMinX(map) + (CGFloat)(cellRow.left) * map.size.width + sidebarGroupMapGap / 2,
                NSMinY(map) + (CGFloat)(cellRow.top) * map.size.height + sidebarGroupMapGap / 2,
                (CGFloat)(cellRow.width) * map.size.width - sidebarGroupMapGap,
                (CGFloat)(cellRow.height) * map.size.height - sidebarGroupMapGap);
            if (cell.size.width <= 0 || cell.size.height <= 0) {
                continue;
            }
            const CGFloat alpha = !cellRow.focused ? 0.28 : cellRow.activeTab ? 0.85 : 0.55;
            [[foreground colorWithAlphaComponent:alpha] setFill];
            [[NSBezierPath bezierPathWithRoundedRect:cell xRadius:2 yRadius:2] fill];
        }
    }

    // The pointer on an open folder's label lifts the whole folder - the
    // label and every row under it - so it reads as one thing, the way the
    // user's browser shows it.
    if (hovering && hoverRow < owner->rows.length() && owner->rows[hoverRow].label && !owner->rows[hoverRow].collapsed) {
        const StringView folder = owner->rows[hoverRow].folder;
        size_t last = hoverRow;
        while (last + 1 < owner->rows.length() && !owner->rows[last + 1].label && owner->rows[last + 1].folder == folder) {
            ++last;
        }
        const NSRect top = owner->rowRect(bounds, hoverRow);
        const NSRect bottom = owner->rowRect(bounds, last);
        const CGFloat until = min(NSMaxY(bottom), NSMaxY(bounds));
        const NSRect group = NSMakeRect(NSMinX(top) + sidebarPillInset, NSMinY(top) + 1, NSWidth(top) - sidebarPillInset * 2, until - NSMinY(top) - 2);
        const CGFloat radius = layeredSurface ? sidebarLayeredPillRadius : sidebarPillRadius;
        [(layeredSurface ? layeredHoverFill : hoverFill) setFill];
        [[NSBezierPath bezierPathWithRoundedRect:group xRadius:radius yRadius:radius] fill];
    }
    for (NSUInteger at = 0; at < count; ++at) {
        const NSRect row = owner->rowRect(bounds, (size_t)(at));
        if (NSMaxY(row) > NSMaxY(bounds)) {
            // A window too short for every tab shows the ones that fit
            // whole; the chords reach the rest. Half a row drawn at the
            // bottom edge is what a list like this must never look like.
            break;
        }
        const BOOL isHovered = hovering && hoverRow == at;
        if (at < owner->rows.length() && owner->rows[at].label) {
            // A folder: its name in small capitals, a chevron before it, the
            // count after it, and - shut with the tab in front inside - a
            // dot saying so. The pointer over it or a row dropped on it
            // lifts it the way hover lifts a tab.
            const TabRow& folderRow = owner->rows[at];
            const BOOL dropHere = dragging && dropOnLabel && dropIndex == at;
            // An open folder under the pointer is lifted whole (below); a
            // shut one is only its label.
            if ((isHovered && folderRow.collapsed) || dropHere) {
                const CGFloat radius = layeredSurface ? sidebarLayeredPillRadius : sidebarPillRadius;
                [(layeredSurface ? layeredHoverFill : hoverFill) setFill];
                [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(row, sidebarPillInset, 1) xRadius:radius yRadius:radius] fill];
            }
            const NSRect chevron = NSMakeRect(NSMinX(bounds) + sidebarTextInset, NSMidY(row) - 5, 10, 10);
            sidebarDrawSymbol(folderRow.collapsed ? @"chevron.right" : @"chevron.down", chevron, dimText);
            NSString* const countText = [NSString stringWithFormat:@"%lu", (unsigned long)(folderRow.members)];
            NSDictionary* const countAttributes = @{
                NSFontAttributeName: [NSFont systemFontOfSize:fontSize - 2],
                NSForegroundColorAttributeName: dimText,
            };
            const NSSize countSize = [countText sizeWithAttributes:countAttributes];
            const CGFloat countRight = textRight;
            [countText drawAtPoint:NSMakePoint(countRight - countSize.width, NSMidY(row) - countSize.height / 2) withAttributes:countAttributes];
            CGFloat nameRight = countRight - countSize.width - 6;
            if (folderRow.collapsed && folderRow.activeInside) {
                const NSRect dot = NSMakeRect(nameRight - 6, NSMidY(row) - 3, 6, 6);
                [[foreground colorWithAlphaComponent:0.85] setFill];
                [[NSBezierPath bezierPathWithOvalInRect:dot] fill];
                nameRight -= 12;
            }
            NSMutableParagraphStyle* const labelStyle = [[[NSMutableParagraphStyle alloc] init] autorelease];
            labelStyle.lineBreakMode = NSLineBreakByTruncatingTail;
            NSDictionary* const labelAttributes = @{
                NSFontAttributeName: [NSFont systemFontOfSize:fontSize - 2 weight:NSFontWeightSemibold],
                NSForegroundColorAttributeName: dimText,
                NSKernAttributeName: @(0.7),
                NSParagraphStyleAttributeName: labelStyle,
            };
            NSString* const name = at < (NSUInteger)(labels.count) ? labels[at] : @"";
            const NSSize nameSize = [name sizeWithAttributes:labelAttributes];
            CGFloat nameLeft = NSMaxX(chevron) + 6;
            const FolderStyle* const style = owner->composer.bookmarks != nullptr ? owner->composer.bookmarks->style(folderRow.folder) : nullptr;
            if (style != nullptr && !style->icon.empty()) {
                const NSRect icon = NSMakeRect(nameLeft, NSMidY(row) - 8, 16, 16);
                sidebarDrawSymbol(sidebarText(style->icon), icon, idleText);
                nameLeft = NSMaxX(icon) + 6;
            }
            if (nameRight > nameLeft) {
                [name drawWithRect:NSMakeRect(nameLeft, NSMidY(row) - nameSize.height / 2, nameRight - nameLeft, nameSize.height) options:NSStringDrawingUsesLineFragmentOrigin attributes:labelAttributes context:nil];
            }
            continue;
        }
        const BOOL isActive = at == active;
        if (layeredSurface) {
            // The layered window's selection is flat, the way the mock drew
            // it: the user looked at the glass pill on the surface and found
            // it one sheet of glass too many. An ink at a low alpha (tabInk
            // above, fg by default) rather than a mix into bg - the surface under it is its own colour, not
            // the terminal's - so it lifts the row toward the text colour
            // on any theme, with a hairline of the same ink around the
            // active one. Hover is the same shape, fainter and unlined.
            if (isActive || isHovered) {
                const NSRect pill = NSInsetRect(sidebarPillFor(bounds, row), 0.5, 0.5);
                NSBezierPath* const shape = [NSBezierPath bezierPathWithRoundedRect:pill xRadius:sidebarLayeredPillRadius yRadius:sidebarLayeredPillRadius];
                [(isActive ? layeredActiveFill : layeredHoverFill) setFill];
                [shape fill];
                if (isActive) {
                    [layeredActiveEdge setStroke];
                    shape.lineWidth = 1;
                    [shape stroke];
                }
            }
        } else if (isActive && glassSurface) {
            // The active row's pill is a floating sheet of glass, parented
            // beside this view and below it (applyPill). A fill here would
            // land on top of it and put the flat tint back.
            //
            // Hover is left as a fill on purpose, and only for the rows that
            // are not active: the user asked for the active tab to float, not
            // for the pointer to carry a pane of glass around the list.
        } else if (isActive || isHovered) {
            [(isActive ? activeFill : hoverFill) setFill];
            [[NSBezierPath bezierPathWithRoundedRect:sidebarPillFor(bounds, row) xRadius:sidebarPillRadius yRadius:sidebarPillRadius] fill];
        }
        if (isActive && !layeredSurface) {
            // Two marks rather than one: the pill, and a cursor-colored
            // bar against the panel's leading edge. cr is guaranteed
            // distinct from bg - the cursor would be invisible in the
            // grid otherwise - so the active tab stays identifiable on a
            // theme where the pill alone is too subtle, which is the
            // whole job of this row.
            [accent setFill];
            NSRectFill(NSMakeRect(NSMinX(row), NSMinY(row) + 4, 3, row.size.height - 8));
        }
        const TabRow* const rowModel = at < owner->rows.length() ? &owner->rows[at] : nullptr;
        const bool bookmarkHead = rowModel != nullptr && rowModel->bookmark != 0 && (!rowModel->grouped || rowModel->groupFirst);
        const bool closedBookmark = rowModel != nullptr && rowModel->closed;
        const BookmarkState state = bookmarkHead ? owner->rowState((size_t)(at)) : BookmarkState::Closed;
        const Bookmark* const bookmark = bookmarkHead && owner->composer.bookmarks != nullptr ? owner->composer.bookmarks->find(rowModel->bookmark) : nullptr;
        const bool runs = bookmark != nullptr && !bookmark->command.empty();
        // The dot is a connection's: only a bookmark that runs a command -
        // an ssh host, most often - has one while open, and a closed one
        // only with news, a host that does not answer. A directory
        // bookmark says it is open by its bright title and its status line;
        // a green dot there read as a live connection. The exited ring is
        // everyone's: it is what a click will reconnect.
        const bool exitedHere = bookmarkHead && rowModel->exited;
        const bool dotShown = bookmarkHead && (exitedHere || (runs && (!closedBookmark || state == BookmarkState::Unreachable)));
        // A closed bookmark reads at the folder line's tier: there is
        // nothing running behind it yet.
        NSDictionary* const attributes = isActive ? activeAttributes : closedBookmark ? closedAttributes : idleAttributes;
        if (rowModel != nullptr && rowModel->afterBookmarks) {
            // The line between the bookmarks and the ordinary tabs, on the
            // boundary of the two rows rather than in a gap of its own, so
            // the rows keep the one height a click is resolved by.
            [groupEdge setFill];
            NSRectFill(NSMakeRect(NSMinX(bounds) + sidebarTextInset, NSMinY(row) - 0.5, textRight - NSMinX(bounds) - sidebarTextInset, 1));
        }
        // The pointer over a row puts the pin in its gutter, in place of
        // the digit or the bookmark's glyph: pin for a tab, a struck pin
        // for a bookmark. The click on it is sidebarTabsPinAt()'s.
        const bool pinShown = isHovered && owner->composer.bookmarks != nullptr && owner->rowPinnable((size_t)(at));
        if (pinShown) {
            const NSRect gutter = NSMakeRect(NSMinX(bounds) + sidebarTextInset - 2, NSMidY(row) - 8, 16, 16);
            sidebarDrawSymbol(bookmarkHead ? @"pin.slash" : @"pin", gutter, foreground);
        }
        if (bookmarkHead) {
            // A bookmark's gutter holds what it is rather than a digit.
            // An SF Symbol, the size of the pin that replaces it on hover:
            // a terminal for one that runs a command, a folder for one that
            // only opens a directory.
            if (!pinShown) {
                const NSRect gutter = NSMakeRect(NSMinX(bounds) + sidebarTextInset - 2, NSMidY(row) - 8, 16, 16);
                sidebarDrawSymbol(runs ? @"terminal" : @"folder", gutter, closedBookmark ? dimText : idleText);
            }
            if (dotShown) {
                // Filled while its child runs; a hollow ring once it has
                // exited; a dim filled one when its host does not answer -
                // told apart by shape and lightness as well as by colour.
                const CGFloat mapRoom = rowModel->groupFirst ? sidebarGroupMapWidth + sidebarGroupMapRight : 0;
                const NSRect dot = NSMakeRect(textRight - mapRoom - sidebarBookmarkDot, NSMidY(row) - sidebarBookmarkDot / 2, sidebarBookmarkDot, sidebarBookmarkDot);
                if (state == BookmarkState::Unreachable) {
                    [[NSColor colorWithSRGBRed:0xb0 / 255.0 green:0x64 / 255.0 blue:0x5e / 255.0 alpha:1] setFill];
                    [[NSBezierPath bezierPathWithOvalInRect:dot] fill];
                } else if (rowModel->exited) {
                    NSBezierPath* const ring = [NSBezierPath bezierPathWithOvalInRect:NSInsetRect(dot, 0.75, 0.75)];
                    ring.lineWidth = 1.5;
                    [[NSColor colorWithSRGBRed:0xe9 / 255.0 green:0xbd / 255.0 blue:0x6e / 255.0 alpha:1] setStroke];
                    [ring stroke];
                } else {
                    [[NSColor colorWithSRGBRed:0x7f / 255.0 green:0xe0 / 255.0 blue:0xa8 / 255.0 alpha:1] setFill];
                    [[NSBezierPath bezierPathWithOvalInRect:dot] fill];
                }
            }
        }
        // cmd+1..9 select tabs, not panes: the digit is the tab's, on its
        // first row only, and the other rows of a group leave the gutter
        // empty. Past nine there is no chord, and an unreachable number
        // would be worse than an empty gutter.
        if (rowModel != nullptr && !pinShown && !bookmarkHead && rowModel->bookmark == 0 && (!rowModel->grouped || rowModel->groupFirst) && rowModel->tab < 9) {
            NSString* const number = [NSString stringWithFormat:@"%lu", (unsigned long)(rowModel->tab + 1)];
            const NSSize numberSize = [number sizeWithAttributes:numberAttributes];
            [number drawAtPoint:NSMakePoint(NSMinX(bounds) + sidebarTextInset, NSMinY(row) + (row.size.height - numberSize.height) / 2) withAttributes:numberAttributes];
        }
        const CGFloat available = textRight - textLeft;
        if (available <= 0) {
            continue;
        }
        // What is running, where it is running, and on which branch. The
        // second and third are empty when nothing is known about the tab
        // - no process to ask, or one this user may not inspect - and an
        // empty line is drawn as nothing rather than as a gap with a
        // claim in it.
        NSString* const lines[3] = {
            labels[at],
            owner->folders.count > at ? owner->folders[at] : @"",
            owner->branches.count > at ? owner->branches[at] : @"",
        };
        NSDictionary* const lineAttributes[3] = {attributes, subAttributes, subAttributes};
        NSFont* const lineIcons[3] = {nil, folderIcon, branchIcon};
        const unichar lineCodepoints[3] = {0, sidebarFolderIcon, sidebarBranchIcon};
        for (size_t which = 0; which < 3; ++which) {
            NSString* const line = lines[which];
            if (line.length == 0) {
                continue;
            }
            NSDictionary* const lineStyle = lineAttributes[which];
            const NSSize size = [line sizeWithAttributes:lineStyle];
            const CGFloat box = sidebarTabsLineTop(which) + (sidebarTabsLineHeight(which) - size.height) / 2;
            const CGFloat left = (CGFloat)(sidebarTabsLineLeft(which, textLeft, iconsAvailable));
            // A bookmark's status line is not a folder: no folder icon, and
            // it starts where the title does.
            const bool statusLine = bookmarkHead && which == 1;
            const CGFloat lineLeft = statusLine ? textLeft : left;
            if (iconsAvailable && lineIcons[which] != nil && !statusLine) {
                // Larger than the text beside it, so centred on the line
                // rather than set on its top.
                const unichar codepoint = lineCodepoints[which];
                const NSSize iconSize = [[NSString stringWithCharacters:&codepoint length:1] sizeWithAttributes:@{NSFontAttributeName: lineIcons[which]}];
                const CGFloat iconTop = sidebarTabsLineTop(which) + (sidebarTabsLineHeight(which) - iconSize.height) / 2;
                sidebarDrawIcon(lineIcons[which], codepoint, NSMakePoint(textLeft, NSMinY(row) + iconTop), dimText);
            }
            // The first row of a group keeps its title clear of the map in
            // the frame's corner, and a bookmark's head row clear of its dot.
            const CGFloat mapRoom = (which == 0 && rowModel != nullptr && rowModel->groupFirst) ? sidebarGroupMapWidth + sidebarGroupMapRight : 0;
            const CGFloat dotRoom = dotShown ? sidebarBookmarkDot + sidebarBookmarkDotGap : 0;
            const NSRect text = NSMakeRect(lineLeft, NSMinY(row) + box, NSMaxX(bounds) - sidebarPillInset - 8 - lineLeft - mapRoom - dotRoom, size.height);
            [line drawWithRect:text options:NSStringDrawingUsesLineFragmentOrigin attributes:lineStyle context:nil];
        }
    }

    // The new-tab row, under the last tab: a plus centred across the
    // panel. Centred rather than aligned with the rows' text, because it
    // is a button and not another entry in the list - the user asked for
    // exactly this after living with it aligned left. It used to sit under
    // a faint rule; the user asked for the rule to go.
    // A row being dragged: a line in the gap it would land in.
    if (dragging && !dropOnLabel) {
        const NSRect gap = owner->rowRect(bounds, (size_t)(dropIndex));
        const CGFloat y = NSMinY(gap);
        [[NSColor colorWithSRGBRed:0x8e / 255.0 green:0xc5 / 255.0 blue:0xff / 255.0 alpha:1] setFill];
        NSRectFill(NSMakeRect(NSMinX(bounds) + sidebarTextInset, y - 1, textRight - NSMinX(bounds) - sidebarTextInset, 2));
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(NSMinX(bounds) + sidebarTextInset - 3, y - 3, 6, 6)] fill];
    }
    const NSRect plusRow = owner->rowRect(bounds, (size_t)(count));
    if (NSMaxY(plusRow) > NSMaxY(bounds)) {
        return;
    }
    if (hovering && hoverRow == count) {
        const NSRect pill = NSInsetRect(plusRow, sidebarPillInset, 2);
        [(layeredSurface ? layeredHoverFill : hoverFill) setFill];
        const CGFloat radius = layeredSurface ? sidebarLayeredPillRadius : sidebarPillRadius;
        [[NSBezierPath bezierPathWithRoundedRect:pill xRadius:radius yRadius:radius] fill];
    }
    NSString* const plus = @"+";
    const NSSize plusSize = [plus sizeWithAttributes:numberAttributes];
    // The separator hairline is the panel's trailing point and not part
    // of the list, so the plus is centred on what is left of the width.
    const CGFloat plusColumn = bounds.size.width - 1;
    [plus drawAtPoint:NSMakePoint(NSMinX(bounds) + (plusColumn - plusSize.width) / 2, NSMinY(plusRow) + (sidebarRowHeight - plusSize.height) / 2) withAttributes:numberAttributes];
}

- (void)setFrameSize:(NSSize)size {
    [super setFrameSize:size];
    // The pill of glass is not a subview of this one - it cannot be, it would
    // cover the row's text - so autoresizing carries it but cannot tell it
    // that the panel has grown too short for the row it sits on. Asked here,
    // after super has taken the new size, because the panel's origin is
    // pinned by its own mask and the height is all the conversion needs.
    if (owner != nullptr) {
        owner->applyPill();
    }
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    // Rebuilt on every bounds change, which is the whole reason this
    // override exists: a resized panel with a stale area lights up rows
    // the pointer is not over.
    if (tracking != nil) {
        [self removeTrackingArea:tracking];
        [tracking release];
    }
    tracking = [[NSTrackingArea alloc] initWithRect:self.bounds options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingActiveInKeyWindow owner:self userInfo:nil];
    [self addTrackingArea:tracking];
}

- (void)dealloc {
    // Paired with the alloc above: the panel's view is released by hand
    // when cmd+b or a reload puts it away, and the tracking area has to
    // go with it rather than outlive the view it points at.
    if (tracking != nil) {
        [self removeTrackingArea:tracking];
        [tracking release];
        tracking = nil;
    }
    [self closePopover];
    [popoverFolder release];
    delete popoverRows;
    [super dealloc];
}

- (void)hoverAt:(NSPoint)point {
    const long long row = [self rowAtPoint:point];
    const BOOL inside = row >= 0;
    if (hovering == inside && (!inside || hoverRow == (NSUInteger)(row))) {
        // A pointer crossing a row it is already on repaints nothing.
        return;
    }
    hovering = inside;
    hoverRow = inside ? (NSUInteger)(row) : 0;
    self.needsDisplay = YES;
    const bool shutLabel = inside && (size_t)(row) < owner->rows.length() && owner->rows[(size_t)(row)].label && owner->rows[(size_t)(row)].collapsed;
    if (shutLabel) {
        [self showPopoverForRow:(size_t)(row)];
    } else if (inside) {
        [self closePopover];
    }
}

- (void)mouseEntered:(NSEvent*)event {
    [self hoverAt:[self convertPoint:event.locationInWindow fromView:nil]];
}

- (void)mouseMoved:(NSEvent*)event {
    [self hoverAt:[self convertPoint:event.locationInWindow fromView:nil]];
}

- (void)mouseExited:(NSEvent*)event {
    (void)event;
    if (!hovering) {
        return;
    }
    hovering = NO;
    self.needsDisplay = YES;
}

- (long long)rowAtPoint:(NSPoint)point {
    const size_t count = (size_t)(owner->labels.count);
    const double* const heights = owner->heights.length() == count && count != 0 ? owner->heights.data() : nullptr;
    return sidebarTabsRowAtHeights(self.bounds.size.height, point.y - NSMinY(self.bounds), heights, count, (double)(owner->listInset()));
}

- (void)mouseDown:(NSEvent*)event {
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    const NSUInteger count = owner->labels.count;
    const long long row = [self rowAtPoint:point];
    pressRow = -1;
    dragging = NO;
    if (row < 0) {
        // Bare panel: it answers nothing, rather than opening a tab for
        // a click nowhere near the plus.
        return;
    }
    if ((NSUInteger)(row) < count) {
        const TabRow& model = owner->rows[(size_t)(row)];
        if (model.label) {
            // A click shuts or opens the folder at once - waiting out the
            // double-click interval to tell the two apart made every click
            // lag. The second click of a double click puts the folder back
            // the way it was and renames it.
            const StringView folder = model.folder;
            if (event.clickCount >= 2) {
                owner->folderToggledByName(folder);
                owner->beginRename(folder);
            } else {
                owner->folderToggled((size_t)(row));
            }
            return;
        }
        if (sidebarTabsPinAt(point.x - NSMinX(self.bounds)) && owner->rowPinnable((size_t)(row))) {
            owner->rowPinned((size_t)(row));
            return;
        }
        // Selected at the press, as before; the same press may still turn
        // into a drag of the row (mouseDragged:).
        pressRow = row;
        pressPoint = point;
        owner->rowSelected((size_t)(row));
        return;
    }
    // The "+" row: a new tab. Folders are made from the context menu.
    owner->tabOpened();
}

// The context menu: what can be done to the row under the pointer - a
// tab, a bookmark, a folder's label - and a new folder anywhere.
- (NSMenu*)menuForEvent:(NSEvent*)event {
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    const long long row = [self rowAtPoint:point];
    const NSUInteger count = owner->labels.count;
    NSMenu* const menu = [[[NSMenu alloc] initWithTitle:@""] autorelease];
    menu.autoenablesItems = NO;
    auto add = [&](NSMenu* into, NSString* title, SEL action, long long tag, NSString* represented) -> NSMenuItem* {
        NSMenuItem* const item = [into addItemWithTitle:title action:action keyEquivalent:@""];
        item.target = self;
        item.tag = (NSInteger)(tag);
        item.representedObject = represented;
        return item;
    };
    if (row >= 0 && (NSUInteger)(row) < count) {
        const TabRow& model = owner->rows[(size_t)(row)];
        if (model.label) {
            add(menu, model.collapsed ? @"Show Contents" : @"Hide Contents", @selector(menuToggle:), row, nil);
            add(menu, @"Rename Folder…", @selector(menuRename:), row, nil);
            NSMenuItem* const iconItem = [menu addItemWithTitle:@"Icon" action:nil keyEquivalent:@""];
            NSMenu* const icons = [[[NSMenu alloc] initWithTitle:@"Icon"] autorelease];
            const FolderStyle* const style = owner->composer.bookmarks != nullptr ? owner->composer.bookmarks->style(model.folder) : nullptr;
            NSString* const current = style != nullptr ? sidebarText(style->icon) : @"";
            NSMenuItem* const none = add(icons, @"No Icon", @selector(menuIcon:), row, @"");
            none.state = current.length == 0 ? NSControlStateValueOn : NSControlStateValueOff;
            [icons addItem:[NSMenuItem separatorItem]];
            for (NSString* const name : sidebarFolderIcons) {
                NSMenuItem* const item = add(icons, name, @selector(menuIcon:), row, name);
                item.image = [NSImage imageWithSystemSymbolName:name accessibilityDescription:nil];
                item.state = [current isEqualToString:name] ? NSControlStateValueOn : NSControlStateValueOff;
            }
            iconItem.submenu = icons;
            [menu addItem:[NSMenuItem separatorItem]];
            add(menu, @"Delete Folder…", @selector(menuDeleteFolder:), row, nil);
            [menu addItem:[NSMenuItem separatorItem]];
        } else {
            NSMenuItem* const moveItem = [menu addItemWithTitle:@"Move to Folder" action:nil keyEquivalent:@""];
            NSMenu* const folders = [[[NSMenu alloc] initWithTitle:@"Move to Folder"] autorelease];
            Vector<StringView> order;
            if (owner->composer.sessions != nullptr) {
                owner->composer.sessions->folders(order);
            }
            for (const StringView folder : order) {
                NSMenuItem* const item = add(folders, sidebarText(folder), @selector(menuMoveToFolder:), row, sidebarText(folder));
                item.state = folder == model.folder ? NSControlStateValueOn : NSControlStateValueOff;
                item.enabled = folder != model.folder;
            }
            if (order.length() != 0) {
                [folders addItem:[NSMenuItem separatorItem]];
            }
            add(folders, @"New Folder…", @selector(menuMoveToNewFolder:), row, nil);
            moveItem.submenu = folders;
            if (!model.folder.empty()) {
                add(menu, @"Remove from Folder", @selector(menuRemoveFromFolder:), row, nil);
            }
            add(menu, model.bookmark != 0 ? @"Rename Bookmark…" : @"Rename Tab…", @selector(menuRenameTab:), row, nil);
            if (owner->rowPinnable((size_t)(row))) {
                const bool pinned = model.bookmark != 0 && owner->composer.bookmarks != nullptr && owner->composer.bookmarks->find(model.bookmark) != nullptr;
                add(menu, pinned ? (model.closed ? @"Remove Bookmark" : @"Unpin Tab") : @"Pin Tab", @selector(menuPin:), row, nil);
            }
            if (!model.closed) {
                [menu addItem:[NSMenuItem separatorItem]];
                add(menu, @"Close Tab", @selector(menuClose:), row, nil);
            }
            [menu addItem:[NSMenuItem separatorItem]];
        }
    }
    add(menu, @"New Folder", @selector(newFolderFromMenu:), -1, nil);
    return menu;
}

- (void)menuMoveToFolder:(NSMenuItem*)item {
    NSString* const folder = item.representedObject;
    owner->rowDropped((size_t)(item.tag), StringView(folder.UTF8String), owner->composer.sessions != nullptr ? owner->composer.sessions->count() : 0);
}

- (void)menuMoveToNewFolder:(NSMenuItem*)item {
    // The row's tab or bookmark is remembered before the folder is made:
    // making it redraws the list and the row's index may move.
    const size_t row = (size_t)(item.tag);
    if (row >= owner->rows.length()) {
        return;
    }
    const TabRow model = owner->rows[row];
    const StringView folder = owner->folderCreated();
    if (folder.empty()) {
        return;
    }
    for (size_t at = 0; at < owner->rows.length(); ++at) {
        const TabRow& now = owner->rows[at];
        if (!now.label && now.bookmark == model.bookmark && now.pane == model.pane && now.closed == model.closed) {
            owner->rowDropped(at, folder, owner->composer.sessions != nullptr ? owner->composer.sessions->count() : 0);
            break;
        }
    }
}

- (void)menuRemoveFromFolder:(NSMenuItem*)item {
    owner->rowDropped((size_t)(item.tag), StringView(), owner->composer.sessions != nullptr ? owner->composer.sessions->count() : 0);
}

- (void)menuPin:(NSMenuItem*)item {
    owner->rowPinned((size_t)(item.tag));
}

- (void)menuClose:(NSMenuItem*)item {
    owner->rowClosed((size_t)(item.tag));
}

- (void)menuToggle:(NSMenuItem*)item {
    owner->folderToggled((size_t)(item.tag));
}

- (void)menuRename:(NSMenuItem*)item {
    const size_t row = (size_t)(item.tag);
    if (row < owner->rows.length() && owner->rows[row].label) {
        owner->beginRename(owner->rows[row].folder);
    }
}

- (void)menuRenameTab:(NSMenuItem*)item {
    owner->beginRenameTab((size_t)(item.tag));
}

- (void)menuIcon:(NSMenuItem*)item {
    const size_t row = (size_t)(item.tag);
    if (row < owner->rows.length() && owner->rows[row].label) {
        NSString* const icon = item.representedObject;
        owner->folderIconChosen(owner->rows[row].folder, StringView(icon.UTF8String));
    }
}

- (void)menuDeleteFolder:(NSMenuItem*)item {
    const size_t row = (size_t)(item.tag);
    if (row < owner->rows.length() && owner->rows[row].label) {
        owner->beginDeleteFolder(owner->rows[row].folder);
    }
}

// The pop-over of a shut folder, shown while the pointer is on its label and
// put away when it moves to another row.
- (void)showPopoverForRow:(size_t)row {
    if (row >= owner->rows.length() || !owner->rows[row].label || !owner->rows[row].collapsed) {
        return;
    }
    NSString* const folder = sidebarText(owner->rows[row].folder);
    if (folderPopover != nil && folderPopover.shown && [popoverFolder isEqualToString:folder]) {
        return;
    }
    [self closePopover];
    if (popoverRows == nullptr) {
        popoverRows = new stl::Vector<TabRow>();
    }
    owner->folderMembers(owner->rows[row].folder, *popoverRows);
    NSMutableArray<NSString*>* const titles = [NSMutableArray array];
    for (size_t at = 0; at < popoverRows->length(); ++at) {
        [titles addObject:owner->rowTitle((*popoverRows)[at])];
    }
    const CGFloat height = sidebarPopoverPad * 2 + sidebarPopoverRow * (CGFloat)(titles.count + 1) + (titles.count != 0 ? sidebarPopoverRule : 0);
    TerminalFolderPopoverView* const content = [[[TerminalFolderPopoverView alloc] initWithFrame:NSMakeRect(0, 0, sidebarPopoverWidth, height)] autorelease];
    content->sidebar = self;
    content->titles = [titles copy];
    content->hover = -1;
    NSViewController* const controller = [[[NSViewController alloc] init] autorelease];
    controller.view = content;
    folderPopover = [[NSPopover alloc] init];
    folderPopover.contentViewController = controller;
    folderPopover.contentSize = NSMakeSize(sidebarPopoverWidth, height);
    folderPopover.behavior = NSPopoverBehaviorTransient;
    folderPopover.animates = NO;
    [popoverFolder release];
    popoverFolder = [folder retain];
    const NSRect anchor = owner->rowRect(self.bounds, row);
    [folderPopover showRelativeToRect:anchor ofView:self preferredEdge:NSRectEdgeMaxX];
}

- (void)closePopover {
    if (folderPopover != nil) {
        [folderPopover close];
        [folderPopover release];
        folderPopover = nil;
    }
}

// A row of the pop-over was clicked: a tab comes forward, a bookmark opens;
// past the last one is "New Tab", made straight into the folder.
- (void)popoverPick:(NSInteger)index {
    NSString* const folder = [[popoverFolder retain] autorelease];
    const size_t count = popoverRows != nullptr ? popoverRows->length() : 0;
    const TabRow row = index >= 0 && (size_t)(index) < count ? (*popoverRows)[(size_t)(index)] : TabRow();
    // Closed on the next turn of the loop, not here: this is called from the
    // pop-over's own mouseDown:, and closing it now would free the view
    // whose method is still on the stack.
    dispatch_async(dispatch_get_main_queue(), ^{
        [self closePopover];
    });
    SessionSet* const sessions = owner->composer.sessions;
    if (sessions == nullptr || index < 0) {
        return;
    }
    if ((size_t)(index) >= count) {
        if (folder != nil) {
            sessions->newSession();
            sessions->dropTab(sessions->activeIndex(), StringView(folder.UTF8String), sessions->count());
        }
    } else if (row.closed) {
        const Bookmark* const bookmark = owner->composer.bookmarks != nullptr ? owner->composer.bookmarks->find(row.bookmark) : nullptr;
        if (bookmark != nullptr) {
            sessions->openBookmark(*bookmark);
        }
    } else {
        sessions->activatePane(row.pane);
    }
    owner->composer.window->requestFrame();
}

- (void)peekCheck {
    if (owner != nullptr) {
        owner->peekCheck();
    }
}

- (BOOL)popoverShown {
    return folderPopover != nil && folderPopover.shown;
}

- (void)newTabFromMenu:(id)sender {
    (void)sender;
    owner->tabOpened();
}

- (void)newFolderFromMenu:(id)sender {
    (void)sender;
    owner->folderCreated();
}

// Where a drag at this point would land: on a folder's label, or into the
// gap nearest the pointer - above the row it is over when in that row's
// upper half, below it otherwise.
- (void)dropTargetAt:(NSPoint)point {
    const size_t count = (size_t)(owner->labels.count);
    const long long row = [self rowAtPoint:point];
    dropOnLabel = NO;
    if (row < 0 || (size_t)(row) >= count) {
        dropIndex = (NSUInteger)(count);
        return;
    }
    if (owner->rows[(size_t)(row)].label) {
        dropOnLabel = YES;
        dropIndex = (NSUInteger)(row);
        return;
    }
    const NSRect rect = owner->rowRect(self.bounds, (size_t)(row));
    dropIndex = (NSUInteger)(point.y < NSMidY(rect) ? row : row + 1);
}

- (void)mouseDragged:(NSEvent*)event {
    if (pressRow < 0) {
        return;
    }
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    if (!dragging) {
        // A click that wobbles is still a click.
        const CGFloat dx = point.x - pressPoint.x;
        const CGFloat dy = point.y - pressPoint.y;
        if (dx * dx + dy * dy < 16) {
            return;
        }
        dragging = YES;
    }
    [self dropTargetAt:point];
    self.needsDisplay = YES;
}

- (void)mouseUp:(NSEvent*)event {
    (void)event;
    if (!dragging || pressRow < 0) {
        pressRow = -1;
        dragging = NO;
        return;
    }
    const size_t row = (size_t)(pressRow);
    const size_t count = owner->rows.length();
    pressRow = -1;
    dragging = NO;
    // The folder the gap or the label belongs to: the label's own; a gap's
    // is the row below it when that is in a folder, else the row above's -
    // so the gap at a folder's end joins the folder, and the gap under the
    // line between the folders and the loose tabs does not.
    StringView folder;
    size_t before = owner->composer.sessions != nullptr ? owner->composer.sessions->count() : 0;
    const size_t at = (size_t)(dropIndex);
    if (dropOnLabel && at < count) {
        folder = owner->rows[at].folder;
    } else {
        const TabRow* const below = at < count ? &owner->rows[at] : nullptr;
        const TabRow* const above = at > 0 && at - 1 < count ? &owner->rows[at - 1] : nullptr;
        if (below != nullptr && !below->label && !below->afterBookmarks) {
            folder = below->folder;
        } else if (above != nullptr) {
            folder = above->folder;
        }
        if (below != nullptr && !below->label && !below->closed) {
            before = below->tab;
        }
    }
    owner->rowDropped(row, folder, before);
    self.needsDisplay = YES;
}

@end

@implementation TerminalEdgeView

- (NSView*)hitTest:(NSPoint)point {
    (void)point;
    return nil;
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (tracking != nil) {
        [self removeTrackingArea:tracking];
        [tracking release];
    }
    tracking = [[NSTrackingArea alloc] initWithRect:self.bounds options:NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow owner:self userInfo:nil];
    [self addTrackingArea:tracking];
}

- (void)dealloc {
    if (tracking != nil) {
        [self removeTrackingArea:tracking];
        [tracking release];
        tracking = nil;
    }
    [super dealloc];
}

- (void)mouseEntered:(NSEvent*)event {
    (void)event;
    if (owner != nullptr) {
        owner->peek();
    }
}

@end

@implementation TerminalPanelHeaderView

- (BOOL)isFlipped {
    return YES;
}

// The toggle takes its own clicks; the rest of the band drags the window,
// done by hand below so that the button is not dragged with it.
- (BOOL)mouseDownCanMoveWindow {
    return NO;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
    (void)event;
    return YES;
}

- (NSRect)toggleRect {
    const CGFloat side = 28;
    return NSMakeRect(leading, (self.bounds.size.height - side) / 2, side, side);
}

- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    if (owner == nullptr) {
        return;
    }
    NSColor* const ink = nsColorFromTerminalColor(owner->composer.vtConfig.config->fg);
    // The sidebar glyph: a window with its left column ruled off, the
    // system's own picture for this control.
    const NSRect button = [self toggleRect];
    const NSRect glyph = NSMakeRect(NSMidX(button) - 6.5, NSMidY(button) - 5.5, 13, 11);
    NSBezierPath* const frame = [NSBezierPath bezierPathWithRoundedRect:glyph xRadius:2.5 yRadius:2.5];
    frame.lineWidth = 1.3;
    [[ink colorWithAlphaComponent:0.8] setStroke];
    [frame stroke];
    NSBezierPath* const rule = [NSBezierPath bezierPath];
    [rule moveToPoint:NSMakePoint(NSMinX(glyph) + 4.5, NSMinY(glyph))];
    [rule lineToPoint:NSMakePoint(NSMinX(glyph) + 4.5, NSMaxY(glyph))];
    rule.lineWidth = 1.3;
    [rule stroke];

    // The active tab's title, centred across the band and kept clear of
    // the toggle on the left and of as much again on the right, so it
    // stays centred on the panel rather than on what is left of it.
    NSArray<NSString*>* const labels = owner->labels;
    const NSUInteger active = (NSUInteger)(owner->active);
    NSString* const title = active < labels.count ? labels[active] : @"";
    if (title.length == 0) {
        return;
    }
    NSMutableParagraphStyle* const style = [[[NSMutableParagraphStyle alloc] init] autorelease];
    style.alignment = NSTextAlignmentCenter;
    style.lineBreakMode = NSLineBreakByTruncatingMiddle;
    NSDictionary* const attributes = @{
        NSFontAttributeName: [NSFont systemFontOfSize:[NSFont smallSystemFontSize]],
        NSForegroundColorAttributeName: [ink colorWithAlphaComponent:0.72],
        NSParagraphStyleAttributeName: style,
    };
    const CGFloat margin = NSMaxX(button) + 8;
    const CGFloat width = self.bounds.size.width - margin * 2;
    if (width <= 0) {
        return;
    }
    const NSSize size = [title sizeWithAttributes:attributes];
    const NSRect line = NSMakeRect(margin, (self.bounds.size.height - size.height) / 2, width, size.height);
    [title drawWithRect:line options:NSStringDrawingUsesLineFragmentOrigin attributes:attributes context:nil];
}

- (void)mouseDown:(NSEvent*)event {
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    if (NSPointInRect(point, [self toggleRect])) {
        // The same path cmd+b takes, so the reserve, the grid and the
        // shell's size all follow exactly as they do for the chord.
        owner->toggle();
        return;
    }
    if (event.clickCount == 2) {
        // A title bar's double click, which this band now is.
        [self.window performZoom:nil];
        return;
    }
    [self.window performWindowDragWithEvent:event];
}

@end

void createSidebarTabsUi(ObjPool& owner, Composer& composer) {
    owner.make<SidebarTabsUi>(composer);
}

@implementation TerminalFolderPopoverView

- (BOOL)isFlipped {
    return YES;
}

- (void)dealloc {
    if (tracking != nil) {
        [self removeTrackingArea:tracking];
        [tracking release];
    }
    [titles release];
    [super dealloc];
}

- (void)updateTrackingAreas {
    if (tracking != nil) {
        [self removeTrackingArea:tracking];
        [tracking release];
    }
    tracking = [[NSTrackingArea alloc] initWithRect:self.bounds options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingActiveAlways owner:self userInfo:nil];
    [self addTrackingArea:tracking];
    [super updateTrackingAreas];
}

// The row under a point: an index into the titles, titles.count for
// "New Tab", -1 for the air.
- (NSInteger)indexAtPoint:(NSPoint)point {
    const NSInteger count = (NSInteger)(titles.count);
    CGFloat top = sidebarPopoverPad;
    for (NSInteger at = 0; at <= count; ++at) {
        if (at == count && count != 0) {
            top += sidebarPopoverRule;
        }
        if (point.y >= top && point.y < top + sidebarPopoverRow) {
            return at;
        }
        top += sidebarPopoverRow;
    }
    return -1;
}

- (NSRect)rectForIndex:(NSInteger)index {
    const NSInteger count = (NSInteger)(titles.count);
    const CGFloat top = sidebarPopoverPad + sidebarPopoverRow * (CGFloat)(index) + (index == count && count != 0 ? sidebarPopoverRule : 0);
    return NSMakeRect(sidebarPopoverPad, top, NSWidth(self.bounds) - sidebarPopoverPad * 2, sidebarPopoverRow);
}

- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    const NSInteger count = (NSInteger)(titles.count);
    NSMutableParagraphStyle* const style = [[[NSMutableParagraphStyle alloc] init] autorelease];
    style.lineBreakMode = NSLineBreakByTruncatingMiddle;
    NSDictionary* const text = @{
        NSFontAttributeName: [NSFont systemFontOfSize:13],
        NSForegroundColorAttributeName: NSColor.labelColor,
        NSParagraphStyleAttributeName: style,
    };
    NSDictionary* const quiet = @{
        NSFontAttributeName: [NSFont systemFontOfSize:13],
        NSForegroundColorAttributeName: NSColor.secondaryLabelColor,
        NSParagraphStyleAttributeName: style,
    };
    for (NSInteger at = 0; at <= count; ++at) {
        const NSRect row = [self rectForIndex:at];
        if (at == hover) {
            [[NSColor.labelColor colorWithAlphaComponent:0.10] setFill];
            [[NSBezierPath bezierPathWithRoundedRect:row xRadius:6 yRadius:6] fill];
        }
        NSString* const line = at < count ? titles[(NSUInteger)(at)] : @"New Tab";
        NSDictionary* const attributes = at < count ? text : quiet;
        CGFloat left = NSMinX(row) + 10;
        if (at == count) {
            // The plus, drawn rather than typed, so it sits where the
            // rows' text starts and lines up with nothing but itself.
            const NSRect plus = NSMakeRect(left, NSMidY(row) - 5, 10, 10);
            [NSColor.secondaryLabelColor setStroke];
            NSBezierPath* const cross = [NSBezierPath bezierPath];
            [cross moveToPoint:NSMakePoint(NSMidX(plus), NSMinY(plus))];
            [cross lineToPoint:NSMakePoint(NSMidX(plus), NSMaxY(plus))];
            [cross moveToPoint:NSMakePoint(NSMinX(plus), NSMidY(plus))];
            [cross lineToPoint:NSMakePoint(NSMaxX(plus), NSMidY(plus))];
            cross.lineWidth = 1.5;
            [cross stroke];
            left = NSMaxX(plus) + 8;
            if (count != 0) {
                [[NSColor.separatorColor colorWithAlphaComponent:0.6] setFill];
                NSRectFill(NSMakeRect(NSMinX(row) + 4, NSMinY(row) - sidebarPopoverRule / 2 - 0.5, NSWidth(row) - 8, 1));
            }
        }
        const NSSize size = [line sizeWithAttributes:attributes];
        [line drawWithRect:NSMakeRect(left, NSMidY(row) - size.height / 2, NSMaxX(row) - 10 - left, size.height) options:NSStringDrawingUsesLineFragmentOrigin attributes:attributes context:nil];
    }
}

- (void)mouseMoved:(NSEvent*)event {
    const NSInteger at = [self indexAtPoint:[self convertPoint:event.locationInWindow fromView:nil]];
    if (at != hover) {
        hover = at;
        self.needsDisplay = YES;
    }
}

- (void)mouseExited:(NSEvent*)event {
    (void)event;
    hover = -1;
    self.needsDisplay = YES;
}

- (void)mouseDown:(NSEvent*)event {
    const NSInteger at = [self indexAtPoint:[self convertPoint:event.locationInWindow fromView:nil]];
    if (at >= 0) {
        [sidebar popoverPick:at];
    }
}

@end
