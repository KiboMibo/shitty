/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "ui_sidebar_tabs.h"

#include "composer.h"
#include "input_bindings.h"
#include "listener.h"
#include "options.h"

#include <plt/platform.h>
#include <plt/platform_headless.h>
#include <plt/window.h>

#include <std/lib/list.h>
#include <std/mem/obj_pool.h>
#include <std/str/view.h>
#include <std/tst/ut.h>

using namespace stl;

// createSidebarTabsUi() is macOS-only chrome and only enters the build
// on darwin (build.py); off it there is nothing to link against. The
// drawing itself is AppKit's and wants a human; what is testable here is
// the half that decides how much of the window the terminal gets - the
// half a mistake in is invisible until the text is already under the
// panel - and the two decisions V2 hoisted out of the AppKit half so
// they could be checked at all.
#if defined(__APPLE__)

// What a row shows instead of the raw window title, and the row a click
// lands in, both defined in ui_sidebar_tabs.mm. Declared here rather
// than in ui_sidebar_tabs.h because they are the module's own business
// and nobody else's; the pattern is F4's csdTabsChromeAlpha().
StringView sidebarTabsShortTitle(StringView title);
long long sidebarTabsRowAt(double panelHeight, double offsetFromTop, size_t count);

namespace {
    // Everything AppKit in this module is deferred to the main queue,
    // which no unit test drains, and the deferred work is the only
    // thing that touches an NSWindow. Leaving composer.sessions null
    // keeps it that way: the projection returns before it schedules
    // anything, so nothing is left pointing at a pool this test is
    // about to drop.
    Composer& sidebarComposer(ObjPool& pool, Options& options) {
        Composer& composer = *pool.make<Composer>(&pool);
        options.border = 0;
        options.sidebarTabs = true;
        options.sidebarWidth = 200;
        composer.opts = &options;
        plt::Platform* const platform = plt::createHeadlessPlatform(pool);
        composer.window = platform->createWindow(pool, {});
        composer.setGlyphSize(8, 16);
        return composer;
    }

    void publish(IntrusiveList& listeners) {
        for (IntrusiveNode* node = listeners.mutFront(); node != listeners.mutEnd();) {
            Listener* const listener = static_cast<Listener*>(node);
            node = node->next;
            listener->onListen();
        }
    }

    bool pressCmdB(Composer& composer) {
        return composer.inputBindings->key({plt::InputKey::Printable, plt::InputAction::Press, plt::InputSuper, 0, 'b'});
    }
}

STD_TEST_SUITE(SidebarTabsUi) {
    // The reserve is in place before the first resize, not after it:
    // application.cpp builds this object right after the window and
    // before showWindow() sizes the grid, so a panel that only claimed
    // its width once something else asked would show one frame of text
    // underneath itself and then jump.
    STD_TEST(ReservesItsWidthBeforeTheGridIsFirstCounted) {
        auto pool = ObjPool::fromMemory();
        Options options;
        Composer& composer = sidebarComposer(*pool, options);

        createSidebarTabsUi(*pool, composer);

        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 200);
        STD_INSIST(composer.chromeReserve(ChromeSide::Top) == 0);
        STD_INSIST(composer.chromeReserve(ChromeSide::Bottom) == 0);
        STD_INSIST(composer.chromeReserve(ChromeSide::Right) == 0);

        composer.resize(1600, 800);

        // 200 points wide out of 1600 leaves 1400, which is 175 cells
        // of 8 - and the rows are untouched, the panel being on no
        // vertical edge. The left edge and nothing else: the title-bar
        // strip's own reserve is on top and stays there, and it is the
        // left one ui_csd_tabs.mm reads to know a tab list is up (V2).
        STD_INSIST(composer.columns == 175);
        STD_INSIST(composer.rows == 50);
        STD_INSIST(composer.contentInsets().left == 200);

        // The width is an option in points and stays one: a move to a
        // Retina display doubles what it costs the grid without anybody
        // re-applying anything.
        composer.setContentScale(2.0f);
        composer.resize(1600, 800);

        STD_INSIST(composer.contentInsets().left == 400);
        STD_INSIST(composer.columns == 150);
    }

    // V2's first complaint, and the one a test can pin down: the panel
    // is on the *left*. Every reserve above says so, and this says the
    // sides are not interchangeable - the grid's own left inset is what
    // moves, and the right one never does.
    STD_TEST(TheGridLosesItsLeftEdgeAndKeepsItsRight) {
        auto pool = ObjPool::fromMemory();
        Options options;
        Composer& composer = sidebarComposer(*pool, options);
        createSidebarTabsUi(*pool, composer);
        composer.resize(1600, 800);

        STD_INSIST(composer.contentInsets().left == 200);
        STD_INSIST(composer.contentInsets().right == 0);
        STD_INSIST(composer.contentInsets().top == 0);
        STD_INSIST(composer.contentInsets().bottom == 0);

        STD_INSIST(pressCmdB(composer));

        STD_INSIST(composer.contentInsets().left == 0);
        STD_INSIST(composer.contentInsets().right == 0);
    }

    // V2's third complaint, the readable half of it: a shell sets the
    // window title to the whole of "user@host:~/Projects/github.com/
    // shitty", and a 220pt column showed the user "...ects/github.com/
    // shitty". The last path component is what differs between tabs.
    STD_TEST(ARowShowsThePathsLastComponentAndLeavesACommandAlone) {
        STD_INSIST(sidebarTabsShortTitle(StringView(u8"kibomibo@KiboMBP:~/Projects/github.com/shitty")) == StringView(u8"shitty"));
        STD_INSIST(sidebarTabsShortTitle(StringView(u8"a/b")) == StringView(u8"b"));

        // No slash at all is a command line, or a bare host name, and
        // there is nothing to cut off it.
        STD_INSIST(sidebarTabsShortTitle(StringView(u8"vim")) == StringView(u8"vim"));
        STD_INSIST(sidebarTabsShortTitle(StringView(u8"npm run build")) == StringView(u8"npm run build"));

        // The edges: a trailing slash would leave an empty label, and an
        // empty title never reaches here (project() substitutes the
        // brand name first), but neither may walk off the end.
        STD_INSIST(sidebarTabsShortTitle(StringView(u8"~/Projects/")) == StringView(u8"~/Projects/"));
        STD_INSIST(sidebarTabsShortTitle(StringView(u8"/")) == StringView(u8"/"));
        STD_INSIST(sidebarTabsShortTitle(StringView()).length() == 0);
    }

    // A click lands in the row the eye sees, which is one function's
    // answer shared by drawRect: and mouseDown: - the two disagreeing is
    // the classic way a list like this selects the tab above the one
    // under the pointer. Three tabs, so row 3 is the new-tab row.
    STD_TEST(AClickLandsInTheRowThatWasDrawnThere) {
        // The panel starts with a gap, and a click in it selects nothing
        // rather than the first tab.
        STD_INSIST(sidebarTabsRowAt(800, 0, 3) == -1);
        STD_INSIST(sidebarTabsRowAt(800, 5, 3) == -1);

        STD_INSIST(sidebarTabsRowAt(800, 6, 3) == 0);
        STD_INSIST(sidebarTabsRowAt(800, 35, 3) == 0);
        STD_INSIST(sidebarTabsRowAt(800, 36, 3) == 1);
        STD_INSIST(sidebarTabsRowAt(800, 95, 3) == 2);

        // The new-tab row under the list, and bare panel below it.
        STD_INSIST(sidebarTabsRowAt(800, 96, 3) == 3);
        STD_INSIST(sidebarTabsRowAt(800, 126, 3) == -1);

        // A row that does not fit whole is drawn nowhere, so it answers
        // nothing either: at 76 points tall the panel shows rows 0 and 1
        // and half of row 2, and the half-row is not clickable.
        STD_INSIST(sidebarTabsRowAt(76, 40, 3) == 1);
        STD_INSIST(sidebarTabsRowAt(76, 70, 3) == -1);
    }

    // cmd+b, and the one thing about it that is not like the hover strip
    // T6 builds: this legitimately re-counts the grid (A7). The columns
    // the panel held come back, and the resize that publishes is what
    // carries the new size to the shell.
    STD_TEST(CmdBGivesTheColumnsBackAndTakesThemAgain) {
        auto pool = ObjPool::fromMemory();
        Options options;
        Composer& composer = sidebarComposer(*pool, options);
        createSidebarTabsUi(*pool, composer);
        composer.resize(1600, 800);

        STD_INSIST(composer.columns == 175);

        STD_INSIST(pressCmdB(composer));

        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 0);
        STD_INSIST(composer.contentInsets().left == 0);
        STD_INSIST(composer.columns == 200);
        STD_INSIST(composer.rows == 50);
        // The window itself did not move; only the share of it the
        // terminal gets did.
        STD_INSIST(composer.pixelWidth == 1600);

        STD_INSIST(pressCmdB(composer));

        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 200);
        STD_INSIST(composer.columns == 175);
    }

    // Without -sidebarTabs the window is the one it always was: nothing
    // is reserved, cmd+b is not a way to make a panel appear that the
    // user never asked for, and the chord is not even consumed - it
    // goes on reaching the program inside the terminal, which under the
    // kitty keyboard protocol is told about it.
    STD_TEST(WithoutTheOptionNothingIsReservedAndCmdBIsNotEvenTaken) {
        auto pool = ObjPool::fromMemory();
        Options options;
        Composer& composer = sidebarComposer(*pool, options);
        options.sidebarTabs = false;

        createSidebarTabsUi(*pool, composer);
        composer.resize(1600, 800);

        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 0);
        STD_INSIST(composer.columns == 200);

        STD_INSIST(!pressCmdB(composer));

        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 0);
        STD_INSIST(composer.columns == 200);
    }

    // A reload that turns the option off has to hand the columns back
    // as well: a reserve nobody draws in is the grid paying rent for a
    // panel that is not there. The same path picks up a new width.
    STD_TEST(AReloadThatDropsTheOptionDropsTheReserve) {
        auto pool = ObjPool::fromMemory();
        Options options;
        Composer& composer = sidebarComposer(*pool, options);
        createSidebarTabsUi(*pool, composer);
        composer.resize(1600, 800);

        STD_INSIST(composer.columns == 175);

        options.sidebarTabs = false;
        publish(composer.configChangedListeners);

        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 0);
        STD_INSIST(composer.columns == 200);

        options.sidebarTabs = true;
        options.sidebarWidth = 400;
        publish(composer.configChangedListeners);

        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 400);
        STD_INSIST(composer.columns == 150);
    }
}

#endif
