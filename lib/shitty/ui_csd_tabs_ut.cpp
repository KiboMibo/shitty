/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "ui_csd_tabs.h"

#include "composer.h"
#include "listener.h"
#include "options.h"

#include <plt/platform.h>
#include <plt/platform_headless.h>
#include <plt/window.h>

#include <std/lib/list.h>
#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

// createCsdTabsUi() is macOS-only chrome and only enters the build on
// darwin (build.py); off it there is nothing to link against.
#if defined(__APPLE__)

// One hover transition of the auto-hiding title bar, defined in
// ui_csd_tabs.mm. Declared here rather than in ui_csd_tabs.h because
// the header is not this task's file to change; it is the only way a
// test can drive the path A7 forbids from touching geometry, an
// NSTrackingArea being out of reach here.
void csdTabsChromeHovered(Composer& composer, bool inside);

// The hover's decision on its own, hoisted out of the AppKit half of
// csdTabsChromeHovered() by F4 so a test can reach it at all (R4-test,
// I2). Declared here for the same reason as the line above.
double csdTabsChromeAlpha(const Composer& composer, bool inside);

namespace {
    // A Composer wired the way application.cpp wires one by the time
    // createCsdTabsUi() runs: a window (headless, so every AppKit path
    // in the module returns early and no NSWindow is ever messaged) and
    // a glyph size, which is what makes a grid countable at all.
    Composer& chromeComposer(ObjPool& pool, Options& options) {
        Composer& composer = *pool.make<Composer>(&pool);
        options.border = 0;
        options.autoHideChrome = true;
        composer.opts = &options;
        plt::Platform* const platform = plt::createHeadlessPlatform(pool);
        composer.window = platform->createWindow(pool, {});
        composer.setGlyphSize(8, 16);
        return composer;
    }

    // What Config::start() does after a reload: walk the list and let
    // every listener re-read the options it projects.
    void publish(IntrusiveList& listeners) {
        for (IntrusiveNode* node = listeners.mutFront(); node != listeners.mutEnd();) {
            Listener* const listener = static_cast<Listener*>(node);
            node = node->next;
            listener->onListen();
        }
    }
}

STD_TEST_SUITE(CsdTabsUi) {
    // A7's whole point, and the first reserve in this codebase that
    // makes contentInsets() asymmetric vertically: the strip is charged
    // to the grid for as long as the mode is on, on the top edge and
    // nowhere else. The number itself is AppKit's title bar height, so
    // the test ties it to the rows it costs instead of writing down a
    // system metric that changed once already (22 before Yosemite, 32
    // measured here).
    STD_TEST(ReservesTheTitleBarStripOnTheTopEdgeAlone) {
        auto pool = ObjPool::fromMemory();
        Options options;
        Composer& composer = chromeComposer(*pool, options);

        createCsdTabsUi(*pool, composer);

        const u16 strip = composer.chromeReserve(ChromeSide::Top);
        STD_INSIST(strip > 0);
        STD_INSIST(composer.chromeReserve(ChromeSide::Right) == 0);
        STD_INSIST(composer.chromeReserve(ChromeSide::Bottom) == 0);
        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 0);

        composer.resize(1600, 800);

        STD_INSIST(composer.contentInsets().top == strip);
        STD_INSIST(composer.contentInsets().left == 0);
        STD_INSIST(composer.columns == 200);
        STD_INSIST(composer.rows == (u16)((800 - strip) / 16));

        // Points, not backing pixels: a move to a Retina display costs
        // the grid twice as much without this module re-applying
        // anything, exactly as the sidebar's width does.
        composer.setContentScale(2.0f);
        composer.resize(1600, 800);

        STD_INSIST(composer.contentInsets().top == (u16)(strip * 2));
        STD_INSIST(composer.rows == (u16)((800 - strip * 2) / 16));
    }

    // R4-test: the one number in A7 that nothing else can check. The
    // reserve has to equal the height the content view *gains* from
    // NSWindowStyleMaskFullSizeContentView, and every test here reads
    // the reserve back out of the Composer that was just told it - so a
    // chromeStripPoints() returning half, or twice, the real title bar
    // is green everywhere and slides two rows of text under the title
    // bar on a live window. Writing the number down is what T6 warned
    // against (it was 22 before Yosemite and is 32 here), so this is a
    // band rather than an equality: wide enough that no macOS title bar
    // has ever fallen outside it, narrow enough that halving or
    // doubling today's answer leaves it. Exact agreement with
    // FullSizeContentView is a live measurement and stays one - T6's
    // report has it at 1708x2082 against 1708x2146, the same 94x59.
    STD_TEST(TheReservedStripIsATitleBarsHeightAndNotAMultipleOfOne) {
        auto pool = ObjPool::fromMemory();
        Options options;
        Composer& composer = chromeComposer(*pool, options);

        createCsdTabsUi(*pool, composer);

        const u16 strip = composer.chromeReserve(ChromeSide::Top);

        STD_INSIST(strip >= 22);
        STD_INSIST(strip < 64);
    }

    // The hard requirement of the whole task: ten passes of the pointer
    // over the strip and back, and the grid is the grid it was. A hover
    // that re-counted it would send the shell a SIGWINCH per crossing
    // and make Vterm rebuild Screen with a scrollback reflow twice per
    // pass.
    STD_TEST(HoverChangesVisibilityAndNothingElse) {
        auto pool = ObjPool::fromMemory();
        Options options;
        Composer& composer = chromeComposer(*pool, options);
        createCsdTabsUi(*pool, composer);
        composer.resize(1600, 800);

        const u16 strip = composer.chromeReserve(ChromeSide::Top);
        const u16 columns = composer.columns;
        const u16 rows = composer.rows;
        STD_INSIST(strip > 0);

        for (int cycle = 0; cycle < 10; ++cycle) {
            csdTabsChromeHovered(composer, true);

            STD_INSIST(composer.chromeReserve(ChromeSide::Top) == strip);
            STD_INSIST(composer.contentInsets().top == strip);
            STD_INSIST(composer.columns == columns);
            STD_INSIST(composer.rows == rows);

            csdTabsChromeHovered(composer, false);

            STD_INSIST(composer.chromeReserve(ChromeSide::Top) == strip);
            STD_INSIST(composer.contentInsets().top == strip);
            STD_INSIST(composer.columns == columns);
            STD_INSIST(composer.rows == rows);
        }
    }

    // Without the option the window is the one it always was, and the
    // grid pays for nothing. noDecorations is the same case reached the
    // other way: there is no title bar to hide, so charging the grid
    // for one would take rows away for nothing at all.
    STD_TEST(WithoutTheOptionNothingIsReserved) {
        auto pool = ObjPool::fromMemory();
        Options options;
        Composer& composer = chromeComposer(*pool, options);
        options.autoHideChrome = false;

        createCsdTabsUi(*pool, composer);
        composer.resize(1600, 800);

        STD_INSIST(composer.chromeReserve(ChromeSide::Top) == 0);
        STD_INSIST(composer.rows == 50);

        options.autoHideChrome = true;
        options.noDecorations = true;
        publish(composer.configChangedListeners);

        STD_INSIST(composer.chromeReserve(ChromeSide::Top) == 0);
        STD_INSIST(composer.rows == 50);
    }

    // A reload turns the mode on and off without a restart, and the
    // rows follow it both ways: a reserve left behind by an option that
    // is gone is the grid paying rent for a strip nobody hides.
    STD_TEST(AReloadTurnsTheModeOnAndGivesTheRowsBackAgain) {
        auto pool = ObjPool::fromMemory();
        Options options;
        Composer& composer = chromeComposer(*pool, options);
        options.autoHideChrome = false;
        createCsdTabsUi(*pool, composer);
        composer.resize(1600, 800);

        STD_INSIST(composer.rows == 50);

        options.autoHideChrome = true;
        publish(composer.configChangedListeners);

        const u16 strip = composer.chromeReserve(ChromeSide::Top);
        STD_INSIST(strip > 0);
        STD_INSIST(composer.rows == (u16)((800 - strip) / 16));

        options.autoHideChrome = false;
        publish(composer.configChangedListeners);

        STD_INSIST(composer.chromeReserve(ChromeSide::Top) == 0);
        STD_INSIST(composer.rows == 50);
    }

    // F4, I2: the three cases of A7's hover, none of which had a test.
    // The mutation R4-test named N13 - the pointer hiding the chrome it
    // should show and showing the chrome it should hide - survived the
    // whole suite, because the decision was a line under an early exit
    // that no headless test reaches. As a function it is checkable
    // without a window at all, and the reserve is checked alongside it:
    // A7's other half is that neither answer costs the grid a row.
    STD_TEST(ThePointerRevealsTheChromeAndTheOptionDecidesWhetherItHides) {
        auto pool = ObjPool::fromMemory();
        Options options;
        Composer& composer = chromeComposer(*pool, options);
        createCsdTabsUi(*pool, composer);
        composer.resize(1600, 800);

        const u16 strip = composer.chromeReserve(ChromeSide::Top);
        const u16 rows = composer.rows;
        STD_INSIST(strip > 0);

        // Away from the strip the title bar is invisible - that is the
        // whole feature.
        STD_INSIST(csdTabsChromeAlpha(composer, false) == 0.0);
        // Under the pointer it comes back.
        STD_INSIST(csdTabsChromeAlpha(composer, true) == 1.0);

        // And with the mode off it is never dimmed, wherever the
        // pointer is: an ordinary window whose title bar faded when the
        // pointer left it would be a defect and not a feature.
        options.autoHideChrome = false;
        publish(composer.configChangedListeners);

        STD_INSIST(csdTabsChromeAlpha(composer, false) == 1.0);
        STD_INSIST(csdTabsChromeAlpha(composer, true) == 1.0);

        // noDecorations reaches the same answer the other way: no title
        // bar exists to hide.
        options.autoHideChrome = true;
        options.noDecorations = true;
        publish(composer.configChangedListeners);

        STD_INSIST(csdTabsChromeAlpha(composer, false) == 1.0);
        STD_INSIST(csdTabsChromeAlpha(composer, true) == 1.0);

        // The grid never heard about any of it. Rows changed once, when
        // the reload dropped the reserve, and not once for a pointer.
        options.noDecorations = false;
        publish(composer.configChangedListeners);

        STD_INSIST(composer.chromeReserve(ChromeSide::Top) == strip);
        STD_INSIST(composer.rows == rows);
    }

    // The bridge cast to NSWindow lives in exactly one place in
    // ui_csd_tabs.mm, and it has to check the render context's backend
    // tag rather than its .window pointer: every backend hands back a
    // non-null .window, the headless one pointing at its own render
    // target. Without the tag check the constructor's own
    // applyTitlebarColor() sent an Objective-C message to that target
    // and took the whole test binary down with SIGSEGV (R2-qa round 2,
    // B5). transparentTitlebar is what carries the constructor past its
    // early exits and into the first message send, so it has to be on
    // for this to test anything.
    STD_TEST(HeadlessWindowIsNeverBridgedToAnNSWindow) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        plt::Platform* const platform = plt::createHeadlessPlatform(*pool);
        composer.window = platform->createWindow(*pool, {});
        Options* const options = pool->make<Options>();
        options->transparentTitlebar = true;
        composer.opts = options;
        STD_INSIST(composer.window->renderContext().backend != plt::RenderBackend::Cocoa);
        STD_INSIST(composer.window->renderContext().window != nullptr);

        createCsdTabsUi(*pool, composer);
    }
}

#endif
