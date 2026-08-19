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
#include <std/tst/ut.h>

using namespace stl;

// createSidebarTabsUi() is macOS-only chrome and only enters the build
// on darwin (build.py); off it there is nothing to link against. The
// drawing and the clicks are AppKit's and want a human; what is testable
// here is the half that decides how much of the window the terminal
// gets, which is the half a mistake in is invisible until the text is
// already under the panel.
#if defined(__APPLE__)

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

        STD_INSIST(composer.chromeReserve(ChromeSide::Right) == 200);
        STD_INSIST(composer.chromeReserve(ChromeSide::Top) == 0);
        STD_INSIST(composer.chromeReserve(ChromeSide::Bottom) == 0);
        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 0);

        composer.resize(1600, 800);

        // 200 points wide out of 1600 leaves 1400, which is 175 cells
        // of 8 - and the rows are untouched, the panel being on no
        // horizontal edge.
        STD_INSIST(composer.columns == 175);
        STD_INSIST(composer.rows == 50);
        STD_INSIST(composer.contentInsets().right == 200);

        // The width is an option in points and stays one: a move to a
        // Retina display doubles what it costs the grid without anybody
        // re-applying anything.
        composer.setContentScale(2.0f);
        composer.resize(1600, 800);

        STD_INSIST(composer.contentInsets().right == 400);
        STD_INSIST(composer.columns == 150);
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

        STD_INSIST(composer.chromeReserve(ChromeSide::Right) == 0);
        STD_INSIST(composer.contentInsets().right == 0);
        STD_INSIST(composer.columns == 200);
        STD_INSIST(composer.rows == 50);
        // The window itself did not move; only the share of it the
        // terminal gets did.
        STD_INSIST(composer.pixelWidth == 1600);

        STD_INSIST(pressCmdB(composer));

        STD_INSIST(composer.chromeReserve(ChromeSide::Right) == 200);
        STD_INSIST(composer.columns == 175);
    }

    // Without -sidebarTabs the window is the one it always was: nothing
    // is reserved, and cmd+b is not a way to make a panel appear that
    // the user never asked for.
    STD_TEST(WithoutTheOptionNothingIsReservedAndCmdBDoesNothing) {
        auto pool = ObjPool::fromMemory();
        Options options;
        Composer& composer = sidebarComposer(*pool, options);
        options.sidebarTabs = false;

        createSidebarTabsUi(*pool, composer);
        composer.resize(1600, 800);

        STD_INSIST(composer.chromeReserve(ChromeSide::Right) == 0);
        STD_INSIST(composer.columns == 200);

        STD_INSIST(pressCmdB(composer));

        STD_INSIST(composer.chromeReserve(ChromeSide::Right) == 0);
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

        STD_INSIST(composer.chromeReserve(ChromeSide::Right) == 0);
        STD_INSIST(composer.columns == 200);

        options.sidebarTabs = true;
        options.sidebarWidth = 400;
        publish(composer.configChangedListeners);

        STD_INSIST(composer.chromeReserve(ChromeSide::Right) == 400);
        STD_INSIST(composer.columns == 150);
    }
}

#endif
