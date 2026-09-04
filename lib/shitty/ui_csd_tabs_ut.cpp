/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "ui_csd_tabs.h"

#include "composer.h"
#include "input_bindings.h"
#include <lib/vterm/listener.h>
#include "options.h"
#include "ui_sidebar_tabs.h"

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

// Whether the title-bar strip shows at all, hoisted out of apply() by V2
// for the same reason and declared here for the same reason again: below
// the nativeWindow() == nil exit no headless test reaches the decision.
bool csdTabsStripShown(const Composer& composer, bool haveTabs);

namespace {
    // A Composer wired the way application.cpp wires one by the time
    // createCsdTabsUi() runs: a window (headless, so every AppKit path
    // in the module returns early and no NSWindow is ever messaged) and
    // a glyph size, which is what makes a grid countable at all.
    Composer& chromeComposer(ObjPool& pool, Options& options) {
        Composer& composer = *pool.make<Composer>(&pool);
        options.border = 0;
        options.autoHideChrome = true;
        composer.setOptions(&options);
        plt::Platform* const platform = plt::createHeadlessPlatform(pool);
        composer.vt.window = platform->createWindow(pool, {});
        composer.vt.setGlyphSize(8, 16);
        return composer;
    }

    // Whether the user can actually see tabs in the title bar. Neither
    // function answers that on its own: one says whether the strip
    // exists, the other whether the chrome it lives inside is visible,
    // and the defect that reached the user lived in the gap between
    // them - the strip was there all along and the pointer just turned
    // the lights on. Composed here rather than in ui_csd_tabs.mm so the
    // production side gains no function only a test asks for.
    bool tabsOnScreen(const Composer& composer, bool pointerInside) {
        return csdTabsStripShown(composer, true) && csdTabsChromeAlpha(composer, pointerInside) > 0.0;
    }

    // cmd+b, through the real binding table, exactly as the input pump
    // delivers it.
    bool pressCmdB(Composer& composer) {
        return composer.inputBindings->key({plt::InputKey::Printable, plt::InputAction::Press, plt::InputSuper, 0, 'b'});
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
    // C11: the mode reserves nothing at all, and that is the change the
    // user asked for. A7 charged the grid a title bar's height so no
    // text could sit under the chrome; the empty band that left above
    // the first row - the terminal's and the sidebar list's alike, since
    // that list insets itself by this same reserve - is what looked
    // wrong on the screen. The title bar is drawn over the top rows on
    // the frames it is visible at all, and nothing above them otherwise.
    STD_TEST(NothingIsReservedSoTheGridReachesTheTopEdge) {
        auto pool = ObjPool::fromMemory();
        Options options;
        Composer& composer = chromeComposer(*pool, options);

        createCsdTabsUi(*pool, composer);

        STD_INSIST(composer.chromeReserve(ChromeSide::Top) == 0);
        STD_INSIST(composer.chromeReserve(ChromeSide::Right) == 0);
        STD_INSIST(composer.chromeReserve(ChromeSide::Bottom) == 0);
        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 0);

        composer.resize(1600, 800);

        STD_INSIST(composer.contentInsets().top == 0);
        STD_INSIST(composer.contentInsets().left == 0);
        STD_INSIST(composer.vt.columns == 200);
        STD_INSIST(composer.vt.rows == 50);

        // And the same on a Retina display: contentInsets() scales the
        // reserve, so a reserve that came back as a scaled title bar
        // would take twice as many rows there and be caught here.
        composer.setContentScale(2.0f);
        composer.resize(1600, 800);

        STD_INSIST(composer.contentInsets().top == 0);
        STD_INSIST(composer.vt.rows == 50);
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
        const u16 columns = composer.vt.columns;
        const u16 rows = composer.vt.rows;
        STD_INSIST(strip == 0);

        for (int cycle = 0; cycle < 10; ++cycle) {
            csdTabsChromeHovered(composer, true);

            STD_INSIST(composer.chromeReserve(ChromeSide::Top) == strip);
            STD_INSIST(composer.contentInsets().top == strip);
            STD_INSIST(composer.vt.columns == columns);
            STD_INSIST(composer.vt.rows == rows);

            csdTabsChromeHovered(composer, false);

            STD_INSIST(composer.chromeReserve(ChromeSide::Top) == strip);
            STD_INSIST(composer.contentInsets().top == strip);
            STD_INSIST(composer.vt.columns == columns);
            STD_INSIST(composer.vt.rows == rows);
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
        STD_INSIST(composer.vt.rows == 50);

        options.autoHideChrome = true;
        options.noDecorations = true;
        publish(composer.vt.configChangedListeners);

        STD_INSIST(composer.chromeReserve(ChromeSide::Top) == 0);
        STD_INSIST(composer.vt.rows == 50);
    }

    // A reload turns the mode on and off without a restart, and since
    // C11 neither direction costs the grid a row: the reserve is zero
    // while the mode is on and zero again after it goes away.
    STD_TEST(AReloadTurnsTheModeOnAndTheRowsStayWhereTheyAre) {
        auto pool = ObjPool::fromMemory();
        Options options;
        Composer& composer = chromeComposer(*pool, options);
        options.autoHideChrome = false;
        createCsdTabsUi(*pool, composer);
        composer.resize(1600, 800);

        STD_INSIST(composer.vt.rows == 50);

        options.autoHideChrome = true;
        publish(composer.vt.configChangedListeners);

        STD_INSIST(composer.chromeReserve(ChromeSide::Top) == 0);
        STD_INSIST(composer.vt.rows == 50);

        options.autoHideChrome = false;
        publish(composer.vt.configChangedListeners);

        STD_INSIST(composer.chromeReserve(ChromeSide::Top) == 0);
        STD_INSIST(composer.vt.rows == 50);
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
        const u16 rows = composer.vt.rows;
        STD_INSIST(strip == 0);

        // Away from the strip the title bar is invisible - that is the
        // whole feature.
        STD_INSIST(csdTabsChromeAlpha(composer, false) == 0.0);
        // Under the pointer it comes back.
        STD_INSIST(csdTabsChromeAlpha(composer, true) == 1.0);

        // And with the mode off it is never dimmed, wherever the
        // pointer is: an ordinary window whose title bar faded when the
        // pointer left it would be a defect and not a feature.
        options.autoHideChrome = false;
        publish(composer.vt.configChangedListeners);

        STD_INSIST(csdTabsChromeAlpha(composer, false) == 1.0);
        STD_INSIST(csdTabsChromeAlpha(composer, true) == 1.0);

        // noDecorations reaches the same answer the other way: no title
        // bar exists to hide.
        options.autoHideChrome = true;
        options.noDecorations = true;
        publish(composer.vt.configChangedListeners);

        STD_INSIST(csdTabsChromeAlpha(composer, false) == 1.0);
        STD_INSIST(csdTabsChromeAlpha(composer, true) == 1.0);

        // The grid never heard about any of it: not one of the reloads
        // moved a row, and not one pointer crossing did either.
        options.noDecorations = false;
        publish(composer.vt.configChangedListeners);

        STD_INSIST(composer.chromeReserve(ChromeSide::Top) == strip);
        STD_INSIST(composer.vt.rows == rows);
    }

    // V3, the user's complaint about the chord: cmd+b was moving the
    // tabs from the side to the top and back, and he wanted it to put
    // them away. So the placement is the option's answer alone, and the
    // chord only decides whether the chosen placement is on the screen.
    //
    // Both modules are built here, in the order application.cpp builds
    // them, so what is checked is the real handoff between them and not
    // a reserve this test wrote itself.
    STD_TEST(TheChordHidesTheTabsInsteadOfMovingThemToTheOtherEdge) {
        auto pool = ObjPool::fromMemory();
        Options options;
        Composer& composer = chromeComposer(*pool, options);
        options.autoHideChrome = false;
        options.sidebarTabs = true;
        options.sidebarWidth = 220;
        createCsdTabsUi(*pool, composer);
        createSidebarTabsUi(*pool, composer);
        composer.resize(1600, 800);
        const u16 columns = composer.vt.columns;

        // Tabs are in the panel, so the title bar has none.
        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 220);
        STD_INSIST(!csdTabsStripShown(composer, true));

        STD_INSIST(pressCmdB(composer));

        // And now they are nowhere: the panel is gone, the strip did not
        // take its place, and the grid has the width back. This is the
        // whole of what the user asked for, and the assertion the old
        // behaviour failed.
        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 0);
        STD_INSIST(!csdTabsStripShown(composer, true));
        STD_INSIST(composer.vt.columns > columns);

        STD_INSIST(pressCmdB(composer));

        // Back where the option says they belong, and not in the title
        // bar on the way.
        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 220);
        STD_INSIST(!csdTabsStripShown(composer, true));
        STD_INSIST(composer.vt.columns == columns);

        // A reload that moves the placement is the other half: now the
        // title bar carries them and the panel claims nothing. Without
        // this line every assertion above would pass on a build where
        // the strip never shows at all.
        options.sidebarTabs = false;
        publish(composer.vt.configChangedListeners);

        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 0);
        STD_INSIST(csdTabsStripShown(composer, true));
        // A lone session still shows no strip: one tab is not a tab bar.
        STD_INSIST(!csdTabsStripShown(composer, false));
    }

    // Without the sidebar the strip is the tab list, and nothing about
    // V2 changed that: this is the negative control for the test above.
    STD_TEST(WithoutTheSidebarTheStripIsTheTabList) {
        auto pool = ObjPool::fromMemory();
        Options options;
        Composer& composer = chromeComposer(*pool, options);
        createCsdTabsUi(*pool, composer);
        createSidebarTabsUi(*pool, composer);
        composer.resize(1600, 800);

        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 0);
        STD_INSIST(csdTabsStripShown(composer, true));
        STD_INSIST(!csdTabsStripShown(composer, false));

        // And cmd+b is not a way to conjure a panel the user never asked
        // for, so it cannot take the strip away either.
        STD_INSIST(!pressCmdB(composer));

        STD_INSIST(csdTabsStripShown(composer, true));
    }

    // The scenario a user actually hit: -sidebarTabs together with
    // -autoHideChrome, pointer to the top edge and away again, ten times
    // over. The chrome has to come back - hiding it for good would be a
    // different defect - and the tabs must not come back with it.
    //
    // Checked composed, not link by link. Separately both halves looked
    // right on the build the user shot: the strip was legitimately
    // there, the alpha legitimately went to one, and the tabs
    // legitimately appeared. Only the product of the two says what is on
    // the screen, so that is what this asserts.
    STD_TEST(TheRevealedChromeCarriesNoTabsWhileTheSidebarIsUp) {
        auto pool = ObjPool::fromMemory();
        Options options;
        Composer& composer = chromeComposer(*pool, options);
        options.sidebarTabs = true;
        options.sidebarWidth = 220;
        createCsdTabsUi(*pool, composer);
        createSidebarTabsUi(*pool, composer);
        composer.resize(1600, 800);

        const u16 strip = composer.chromeReserve(ChromeSide::Top);
        const u16 columns = composer.vt.columns;
        const u16 rows = composer.vt.rows;
        STD_INSIST(strip == 0);
        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 220);

        for (int cycle = 0; cycle < 10; ++cycle) {
            csdTabsChromeHovered(composer, true);

            // The decoration is revealed - that is the whole of A7 - and
            // it is revealed empty.
            STD_INSIST(csdTabsChromeAlpha(composer, true) == 1.0);
            STD_INSIST(!tabsOnScreen(composer, true));

            csdTabsChromeHovered(composer, false);

            STD_INSIST(csdTabsChromeAlpha(composer, false) == 0.0);
            STD_INSIST(!tabsOnScreen(composer, false));

            // And A7's other half survives V2 unchanged: a pass of the
            // pointer costs the grid nothing.
            STD_INSIST(composer.chromeReserve(ChromeSide::Top) == strip);
            STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 220);
            STD_INSIST(composer.vt.columns == columns);
            STD_INSIST(composer.vt.rows == rows);
        }

        // cmd+b puts the panel away, and the tabs go with it rather than
        // reappearing overhead - the chord hides them, it does not move
        // them (V3).
        STD_INSIST(pressCmdB(composer));

        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 0);
        STD_INSIST(!tabsOnScreen(composer, true));

        // The control that keeps every assertion above from passing for
        // the wrong reason. Move the placement to the title bar and the
        // pointer becomes exactly what decides whether the tabs are on
        // the screen: an implementation where the hover reveals nothing,
        // or where the strip never shows at all, fails right here
        // instead of sailing through a suite of negatives.
        options.sidebarTabs = false;
        publish(composer.vt.configChangedListeners);

        STD_INSIST(tabsOnScreen(composer, true));
        STD_INSIST(!tabsOnScreen(composer, false));

        // And away from the strip they are invisible again, which is
        // A7's half of the same picture.
        STD_INSIST(!tabsOnScreen(composer, false));
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
        composer.vt.window = platform->createWindow(*pool, {});
        Options* const options = pool->make<Options>();
        options->transparentTitlebar = true;
        composer.setOptions(options);
        STD_INSIST(composer.vt.window->renderContext().backend != plt::RenderBackend::Cocoa);
        STD_INSIST(composer.vt.window->renderContext().window != nullptr);

        createCsdTabsUi(*pool, composer);
    }
}

#endif
