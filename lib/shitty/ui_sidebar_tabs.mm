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
#include "session.h"

#include <plt/window.h>

#include <std/ios/fs_utils.h>
#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>
#include <std/str/view.h>
#include <std/sys/throw.h>

#include <libproc.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define Point MacLegacyPoint
#define Rect MacLegacyRect

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#undef Rect
#undef Point

// After AppKit, and it has to be: see the header.
#include "ui_window_tint.h"

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
    @private
    // The row under the pointer, and whether there is one at all. Two
    // fields rather than a sentinel index because Objective-C zeroes
    // ivars, and a zeroed sentinel would light up row zero before the
    // pointer ever entered the panel.
    NSUInteger hoverRow;
    BOOL hovering;
    NSTrackingArea* tracking;
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
        // The other two lines of every row, in step with labels by
        // index. Empty means "nothing known", which is not the same as
        // the branch line's "no git" and must not render as it.
        NSArray<NSString*>* folders = nil;
        NSArray<NSString*>* branches = nil;
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
    NSString* shittySidebarText(StringView view) {
        Buffer buffer(view);
        NSString* const text = [NSString stringWithUTF8String:buffer.cStr()];
        return text == nil ? @"" : text;
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
    static const CGFloat shittySidebarPanelTint = 0.06;

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
    static NSColor* shittySidebarMix(NSColor* background, NSColor* foreground, CGFloat fraction) {
        return [background blendedColorWithFraction:fraction ofColor:foreground];
    }

    // Points. The row height is the panel's own metric rather than a
    // multiple of the cell: the list is chrome, drawn by AppKit in
    // AppKit's units, and lining it up with a grid it does not overlap
    // would buy nothing.
    // Three stacked lines - what is running, which folder, which branch -
    // plus the padding above and below them. Derived rather than written
    // down, so the row can never be too short for what it draws.
    static const CGFloat shittySidebarRowPad = 7;
    static const CGFloat shittySidebarTitleLine = 15;
    static const CGFloat shittySidebarSubLine = 13;
    static const CGFloat shittySidebarRowHeight = shittySidebarRowPad * 2 + shittySidebarTitleLine + shittySidebarSubLine * 2;
    // The gap above the first row, so the list does not start flush
    // against the window's top edge.
    static const CGFloat shittySidebarListTop = 6;
    // How far the active/hovered pill stays clear of the panel's edges,
    // and how far the text sits inside the pill.
    static const CGFloat shittySidebarPillInset = 6;
    static const CGFloat shittySidebarTextInset = shittySidebarPillInset + 10;
    // The number gutter: cmd+1..9 select tabs (InputActions::SelectTab1
    // and on), so the row says which digit it answers to. Past nine
    // there is no chord and the gutter is left empty rather than filled
    // with a number that does nothing.
    static const CGFloat shittySidebarNumberGutter = 18;
    // The icon column on the folder and branch lines. One width for both,
    // so the two texts share a left edge whatever is drawn to the left of
    // them; zero when the glyphs are not in the font at all.
    static const CGFloat shittySidebarIconColumn = 16;
    // Nerd Font code points, the user's own pick: nf-fa-folder and
    // nf-dev-git_branch. Both are in the Private Use Area and both are
    // single UTF-16 units, so a UniChar carries either whole.
    static const unichar shittySidebarFolderIcon = 0xF07B;
    static const unichar shittySidebarBranchIcon = 0xE725;
    static const CGFloat shittySidebarPillRadius = 6;
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

// The tab's working directory, asked of the shell process itself rather
// than of the shell's cooperation. OSC 7 is the usual route and it is a
// dead end here: on macOS only /etc/zshrc_Apple_Terminal installs
// update_terminal_cwd, and it is sourced only under Apple's own
// terminal, so a shitty window never sees the escape at all. The
// process's own cdir is what iTerm2 and Ghostty read, it needs no shell
// integration, and it is exactly what `cd` moves.
//
// False means "no directory to be had" - no such process, or one this
// user may not inspect. That is deliberately a different answer from
// sidebarTabsBranch()'s false, which means "a directory, and no
// repository above it": the row renders the first as nothing at all and
// only the second as "no git", so an empty line can never stand for both
// at once.
bool sidebarTabsDirectory(pid_t pid, Buffer& out) {
    out.reset();
    if (pid <= 0) {
        return false;
    }
    struct proc_vnodepathinfo info;
    // Poisoned rather than left as it came off the stack, so a partial
    // answer can never be mistaken for a whole one. proc_pidinfo reports
    // failure by returning 0, not a negative, so a caller checking only
    // for negatives reads whatever was in this buffer - which on a fresh
    // stack page is zeroes, reads as a plain empty path, and is
    // indistinguishable from an honest refusal. Filled with a byte that
    // is not a terminator it is distinguishable, which is what makes the
    // short-read check below a check a test can show the need for.
    memset(&info, 0xFF, sizeof(info));
    if (proc_pidinfo(pid, PROC_PIDVNODEPATHINFO, 0, &info, sizeof(info)) != (int)(sizeof(info))) {
        return false;
    }
    const size_t length = strnlen(info.pvi_cdir.vip_path, sizeof(info.pvi_cdir.vip_path));
    if (length == 0) {
        return false;
    }
    out.append(info.pvi_cdir.vip_path, length);
    return true;
}

// The git branch a row shows on its third line, and the two pure halves
// of working it out. Both are non-static and declared again in
// ui_sidebar_tabs_ut.cpp for the reason the two above are: a decision
// reachable only through the filesystem is a decision no test pins down.

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
    return shittySidebarRowPad + (line == 0 ? 0 : shittySidebarTitleLine + shittySidebarSubLine * (double)(line - 1));
}

double sidebarTabsLineHeight(size_t line) {
    return line == 0 ? shittySidebarTitleLine : shittySidebarSubLine;
}

double sidebarTabsRowHeight() {
    return shittySidebarRowHeight;
}

double sidebarTabsListTop() {
    return shittySidebarListTop;
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
NSFont* shittySidebarFontCovering(StringView fontName, unichar codepoint, CGFloat size) {
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
    void shittySidebarDrawIcon(NSFont* font, unichar codepoint, NSPoint at, NSColor* color) {
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
    shittySidebarDrawIcon(shittySidebarFontCovering(fontName, (unichar)(codepoint), (CGFloat)(size)), (unichar)(codepoint), NSMakePoint(2, 2), NSColor.blackColor);
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
    return shittySidebarFontCovering(fontName, (unichar)(codepoint), 13) != nil;
}

// Where a row's line starts. Line 0 is the title and sits flush; the
// folder and branch lines share one indent, whether or not an icon is
// drawn in it - which is what keeps them aligned with each other when
// the font has no glyphs and the column collapses to nothing.
double sidebarTabsLineLeft(size_t line, double textLeft, bool iconsAvailable) {
    if (line == 0) {
        return textLeft;
    }
    return textLeft + (iconsAvailable ? shittySidebarIconColumn : 0);
}

// The row an offset down from the panel's top edge falls in: an index
// into the list, `count` for the new-tab row under it, or -1 for panel
// that answers nothing. One function, so drawing and clicking can never
// disagree about where a row is - including at the bottom edge, where a
// row that does not fit whole is drawn nowhere and so answers nothing
// either.
long long sidebarTabsRowAt(double panelHeight, double offsetFromTop, size_t count, double topInset) {
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
    //
    // Zero leaves every line below identical to what it computed before
    // the parameter existed, which is the case with no chrome to reserve.
    const double listTop = topInset + shittySidebarListTop;
    const double offset = offsetFromTop - listTop;
    if (offset < 0) {
        return -1;
    }
    const long long row = (long long)(offset / shittySidebarRowHeight);
    if (row > (long long)(count)) {
        return -1;
    }
    const double bottom = listTop + shittySidebarRowHeight * (double)(row + 1);
    return bottom <= panelHeight ? row : -1;
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
    //
    // The left edge, which is also the signal ui_csd_tabs.mm reads to
    // decide the title-bar strip is redundant (V2): a non-zero reserve
    // on this side means a tab list is already on the screen.
    composer.setChromeReserve(ChromeSide::Left, shown() ? widthPoints() : 0);
}

void SidebarTabsUi::project() {
    SessionSet* const sessions = composer.sessions;
    if (sessions == nullptr) {
        return;
    }
    const size_t count = sessions->count();
    NSMutableArray<NSString*>* const next = [NSMutableArray arrayWithCapacity:(NSUInteger)(count)];
    NSMutableArray<NSString*>* const nextFolders = [NSMutableArray arrayWithCapacity:(NSUInteger)(count)];
    NSMutableArray<NSString*>* const nextBranches = [NSMutableArray arrayWithCapacity:(NSUInteger)(count)];
    Buffer directory;
    Buffer branch;
    for (size_t at = 0; at < count; ++at) {
        // A tab whose shell never set a title shows the brand name,
        // like a fresh window does. The title goes on the row whole:
        // it is the line that says what is *running*, and cutting it
        // down to a path component would make it a second copy of the
        // folder line below it.
        StringView title = sessions->title(at);
        if (title.length() == 0) {
            title = composer.brand->displayName();
        }
        [next addObject:shittySidebarText(title)];

        // Read here rather than cached: this runs on a title change and
        // on any change to the set of tabs, which is exactly when a
        // directory or a branch can have moved, and no oftener. One
        // stat-and-read per tab, measured at well under a tenth of a
        // millisecond.
        if (sidebarTabsDirectory(sessions->pid(at), directory)) {
            [nextFolders addObject:shittySidebarText(sidebarTabsShortTitle(StringView(directory)))];
            [nextBranches addObject:sidebarTabsBranch(StringView(directory), branch) ? shittySidebarText(StringView(branch)) : @"no git"];
        } else {
            // Nothing is known about this tab beyond its title - no
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
        view = [[ShittySidebarView alloc] initWithFrame:frame];
        // Pinned to the left edge and as tall as the content: the same
        // strip Composer::contentInsets() keeps the grid out of, so the
        // two never disagree about where the terminal begins. The
        // content view is not flipped, so leaving both vertical margins
        // fixed keeps the title-bar gap at the top where it belongs.
        view.autoresizingMask = NSViewMaxXMargin | NSViewHeightSizable;
        view.wantsLayer = YES;
        view->owner = this;
        [content addSubview:view];
        if (composer.opts->verbose) {
            fprintf(stderr, "%s: sidebar: tab list installed on the left edge\n", composer.brand->identifierCString());
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
    // Every shade here is opts->fg mixed into opts->bg, rather than a
    // system label tier, for the reason spelled out at length in
    // ui_csd_tabs.mm: the tiers are tuned for the standard material and
    // know nothing about opts->bg, and a light system theme over a dark
    // terminal background made them effectively invisible (issue 84).
    // The panel itself is a shade off the terminal's background, which
    // is what makes it read as a panel rather than as grid with no text
    // in it - the iTerm2 and Ghostty treatment.
    NSColor* const background = nsColorFromTerminalColor(owner->composer.opts->bg);
    NSColor* const foreground = nsColorFromTerminalColor(owner->composer.opts->fg);
    NSColor* const accent = nsColorFromTerminalColor(owner->composer.opts->cr);
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
        : shittySidebarMix(background, foreground, shittySidebarPanelTint);
    const auto shade = [&](CGFloat fraction) {
        return ownColour
            ? shittySidebarMix(panel, foreground, (fraction - shittySidebarPanelTint) / (1.0 - shittySidebarPanelTint))
            : shittySidebarMix(background, foreground, fraction);
    };
    NSColor* const separator = shade(0.30);
    NSColor* const rule = shade(0.16);
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
    const CGFloat tint = windowTintAlpha(owner->composer, self.window);
    if (tint >= 1.0) {
        [panel setFill];
        NSRectFill(bounds);
    } else {
        const TintCoat coat = thinnestCoat(terminalColorFromNsColor(panel), owner->composer.opts->bg);
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
    const CGFloat listInset = (CGFloat)(owner->composer.chromeReserve(ChromeSide::Top));

    // The seam with the grid, on the trailing edge now that the panel is
    // on the left. Visible on purpose: a hairline this close to the
    // background was the "where does the terminal start" complaint.
    [separator setFill];
    NSRectFill(NSMakeRect(NSMaxX(bounds) - 1, NSMinY(bounds), 1, bounds.size.height));

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
            folderIcon = shittySidebarFontCovering(fontnames[at], shittySidebarFolderIcon, fontSize - 1);
        }
        if (branchIcon == nil) {
            branchIcon = shittySidebarFontCovering(fontnames[at], shittySidebarBranchIcon, fontSize - 1);
        }
    }
    // All or nothing. One icon without the other would put the folder and
    // branch lines on different left edges, and a row whose two context
    // lines do not line up looks broken in a way a missing icon does not.
    const bool iconsAvailable = folderIcon != nil && branchIcon != nil;

    NSArray<NSString*>* const labels = owner->labels;
    const NSUInteger count = labels.count;
    const NSUInteger active = (NSUInteger)(owner->active);
    const CGFloat textLeft = NSMinX(bounds) + shittySidebarTextInset + shittySidebarNumberGutter;
    const CGFloat textRight = NSMaxX(bounds) - shittySidebarPillInset - 8;
    for (NSUInteger at = 0; at < count; ++at) {
        const NSRect row = NSMakeRect(NSMinX(bounds), NSMinY(bounds) + listInset + shittySidebarListTop + shittySidebarRowHeight * (CGFloat)(at), bounds.size.width, shittySidebarRowHeight);
        if (NSMaxY(row) > NSMaxY(bounds)) {
            // A window too short for every tab shows the ones that fit
            // whole; the chords reach the rest. Half a row drawn at the
            // bottom edge is what a list like this must never look like.
            break;
        }
        const BOOL isActive = at == active;
        const BOOL isHovered = hovering && hoverRow == at;
        if (isActive || isHovered) {
            // A pill inset from both edges rather than a full-bleed
            // fill: it is what says "one row of a list" instead of "the
            // panel changed color here".
            const NSRect pill = NSInsetRect(row, shittySidebarPillInset, 2);
            [(isActive ? activeFill : hoverFill) setFill];
            [[NSBezierPath bezierPathWithRoundedRect:pill xRadius:shittySidebarPillRadius yRadius:shittySidebarPillRadius] fill];
        }
        if (isActive) {
            // Two marks rather than one: the pill, and a cursor-colored
            // bar against the panel's leading edge. cr is guaranteed
            // distinct from bg - the cursor would be invisible in the
            // grid otherwise - so the active tab stays identifiable on a
            // theme where the pill alone is too subtle, which is the
            // whole job of this row.
            [accent setFill];
            NSRectFill(NSMakeRect(NSMinX(row), NSMinY(row) + 4, 3, row.size.height - 8));
        }
        NSDictionary* const attributes = isActive ? activeAttributes : idleAttributes;
        if (at < 9) {
            // cmd+1..9 select tabs; past nine there is no chord, and an
            // unreachable number would be worse than an empty gutter.
            NSString* const number = [NSString stringWithFormat:@"%lu", (unsigned long)(at + 1)];
            const NSSize numberSize = [number sizeWithAttributes:numberAttributes];
            [number drawAtPoint:NSMakePoint(NSMinX(bounds) + shittySidebarTextInset, NSMinY(row) + (row.size.height - numberSize.height) / 2) withAttributes:numberAttributes];
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
        const unichar lineCodepoints[3] = {0, shittySidebarFolderIcon, shittySidebarBranchIcon};
        for (size_t which = 0; which < 3; ++which) {
            NSString* const line = lines[which];
            if (line.length == 0) {
                continue;
            }
            NSDictionary* const lineStyle = lineAttributes[which];
            const NSSize size = [line sizeWithAttributes:lineStyle];
            const CGFloat box = sidebarTabsLineTop(which) + (sidebarTabsLineHeight(which) - size.height) / 2;
            const CGFloat left = (CGFloat)(sidebarTabsLineLeft(which, textLeft, iconsAvailable));
            if (iconsAvailable && lineIcons[which] != nil) {
                shittySidebarDrawIcon(lineIcons[which], lineCodepoints[which], NSMakePoint(textLeft, NSMinY(row) + box), dimText);
            }
            const NSRect text = NSMakeRect(left, NSMinY(row) + box, NSMaxX(bounds) - shittySidebarPillInset - 8 - left, size.height);
            [line drawWithRect:text options:NSStringDrawingUsesLineFragmentOrigin attributes:lineStyle context:nil];
        }
    }

    // The new-tab row, under the last tab: a plus centred across the
    // panel, with a faint rule over it separating it from the list.
    // Centred rather than aligned with the rows' text, because it is a
    // button and not another entry in the list - the user asked for
    // exactly this after living with it aligned left.
    const NSRect plusRow = NSMakeRect(NSMinX(bounds), NSMinY(bounds) + listInset + shittySidebarListTop + shittySidebarRowHeight * (CGFloat)(count), bounds.size.width, shittySidebarRowHeight);
    if (NSMaxY(plusRow) > NSMaxY(bounds)) {
        return;
    }
    [rule setFill];
    NSRectFill(NSMakeRect(NSMinX(bounds) + shittySidebarTextInset, NSMinY(plusRow), bounds.size.width - shittySidebarTextInset * 2, 1));
    if (hovering && hoverRow == count) {
        const NSRect pill = NSInsetRect(plusRow, shittySidebarPillInset, 2);
        [hoverFill setFill];
        [[NSBezierPath bezierPathWithRoundedRect:pill xRadius:shittySidebarPillRadius yRadius:shittySidebarPillRadius] fill];
    }
    NSString* const plus = @"+";
    const NSSize plusSize = [plus sizeWithAttributes:numberAttributes];
    // The separator hairline is the panel's trailing point and not part
    // of the list, so the plus is centred on what is left of the width.
    const CGFloat plusColumn = bounds.size.width - 1;
    [plus drawAtPoint:NSMakePoint(NSMinX(bounds) + (plusColumn - plusSize.width) / 2, NSMinY(plusRow) + (shittySidebarRowHeight - plusSize.height) / 2) withAttributes:numberAttributes];
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
    [super dealloc];
}

- (void)hoverAt:(NSPoint)point {
    const long long row = sidebarTabsRowAt(self.bounds.size.height, point.y - NSMinY(self.bounds), (size_t)(owner->labels.count), (double)(owner->composer.chromeReserve(ChromeSide::Top)));
    const BOOL inside = row >= 0;
    if (hovering == inside && (!inside || hoverRow == (NSUInteger)(row))) {
        // A pointer crossing a row it is already on repaints nothing.
        return;
    }
    hovering = inside;
    hoverRow = inside ? (NSUInteger)(row) : 0;
    self.needsDisplay = YES;
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

- (void)mouseDown:(NSEvent*)event {
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    const NSUInteger count = owner->labels.count;
    const long long row = sidebarTabsRowAt(self.bounds.size.height, point.y - NSMinY(self.bounds), (size_t)(count), (double)(owner->composer.chromeReserve(ChromeSide::Top)));
    if (row < 0) {
        // Bare panel: it answers nothing, rather than opening a tab for
        // a click nowhere near the plus.
        return;
    }
    if ((NSUInteger)(row) < count) {
        owner->tabSelected((size_t)(row));
        return;
    }
    owner->tabOpened();
}

@end

void createSidebarTabsUi(ObjPool& owner, Composer& composer) {
    owner.make<SidebarTabsUi>(composer);
}
