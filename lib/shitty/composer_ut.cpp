/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "composer.h"

#include "cell_extra_store.h"
#include "grid_geometry.h"
#include "input_bindings.h"
#include "listener.h"
#include "mouse_frontend.h"
#include "options.h"

#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

using namespace stl;

namespace {
    // -verbose is a stream of lines on stderr and nothing in the tree
    // reads it back, which is how the trace that was supposed to prove
    // A7 came to be blind: R4-qa's cmd+b changed the grid three times
    // and printed nothing, and every test stayed green. A pipe over the
    // descriptor is the whole instrument needed to make a missing line
    // a failing test (F4, Q2).
    struct StderrCapture {
        StderrCapture() {
            fflush(stderr);
            STD_INSIST(pipe(fds) == 0);
            saved = dup(STDERR_FILENO);
            STD_INSIST(saved >= 0);
            STD_INSIST(dup2(fds[1], STDERR_FILENO) >= 0);
        }

        // Puts the real stderr back and returns what was written in the
        // meantime, NUL-terminated. Read after both write ends are gone,
        // so an empty capture returns instead of blocking; a trace line
        // is orders of magnitude below the pipe buffer.
        size_t restore(char* out, size_t size) {
            fflush(stderr);
            STD_INSIST(dup2(saved, STDERR_FILENO) >= 0);
            close(saved);
            close(fds[1]);
            const ssize_t got = read(fds[0], out, size - 1);
            close(fds[0]);
            const size_t length = got > 0 ? (size_t)(got) : 0;
            out[length] = '\0';
            return length;
        }

        int fds[2] = {-1, -1};
        int saved = -1;
    };

    struct StateListener final: public Listener {
        explicit StateListener(Composer& composer);

        void onListen(void* argument) override;

        Composer& composer;
        CellExtraStore* extras = nullptr;
        u16 columns = 0;
        u16 rows = 0;
        u16 pixelWidth = 0;
        u16 pixelHeight = 0;
        float contentScale = 0.0f;
        size_t calls = 0;
        bool argumentWasNull = false;
    };

    struct RemovingListener final: public Listener {
        void onListen(void* argument) override;

        size_t calls = 0;
    };
}

StateListener::StateListener(Composer& composer_)
    : composer(composer_)
{
}

void StateListener::onListen(void* argument) {
    extras = composer.cellExtras;
    columns = composer.columns;
    rows = composer.rows;
    pixelWidth = composer.pixelWidth;
    pixelHeight = composer.pixelHeight;
    contentScale = composer.contentScale;
    ++calls;
    argumentWasNull = argument == nullptr;
}

void RemovingListener::onListen(void*) {
    ++calls;
    unlink();
}

STD_TEST_SUITE(Composer) {
    STD_TEST(ConstructsCoreComponents) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());

        STD_INSIST(composer.pool == pool.mutPtr());
        STD_INSIST(composer.cellExtras != nullptr);
        STD_INSIST(composer.smallObjects != nullptr);
        STD_INSIST(composer.input != nullptr);
        STD_INSIST(composer.inputBindings != nullptr);
        STD_INSIST(composer.inputHandlers.front() != composer.inputHandlers.end());
        STD_INSIST(composer.fontResolvers.front() != composer.fontResolvers.end());
    }

    STD_TEST(PublishesContentScaleOnlyAfterChange) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        StateListener listener(composer);
        composer.contentScaleChangedListeners.pushBack(&listener);

        composer.setContentScale(1.5f);

        STD_INSIST(composer.contentScale == 1.5f);
        STD_INSIST(listener.contentScale == 1.5f);
        STD_INSIST(listener.calls == 1);
        STD_INSIST(listener.argumentWasNull);

        composer.setContentScale(1.5f);

        STD_INSIST(listener.calls == 1);
    }

    STD_TEST(DerivesPhysicalBorderFromSnapshotAndScale) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        options.border = 7;
        composer.opts = &options;

        STD_INSIST(composer.borderPixels() == 7);

        composer.setContentScale(1.5f);

        STD_INSIST(composer.borderPixels() == 11);
    }

    STD_TEST(PublishesCommittedResizeState) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        StateListener listener(composer);
        composer.resizedListeners.pushBack(&listener);
        composer.setGlyphSize(8, 16);
        const Insets insets = composer.contentInsets();
        const u16 width = (u16)(gridPixelWidth(10, insets, composer.glyphWidth) + 3);
        const u16 height = (u16)(gridPixelHeight(4, insets, composer.glyphHeight) + 7);

        composer.resize(width, height);

        STD_INSIST(listener.calls == 1);
        STD_INSIST(listener.columns == 10);
        STD_INSIST(listener.rows == 4);
        STD_INSIST(listener.pixelWidth == width);
        STD_INSIST(listener.pixelHeight == height);

        composer.resize(width, height);

        STD_INSIST(listener.calls == 1);

        composer.resize(width + 1, height);

        STD_INSIST(listener.calls == 2);
        STD_INSIST(listener.columns == 10);
        STD_INSIST(listener.pixelWidth == width + 1);
    }

    STD_TEST(PublishesCellExtraReplacement) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        StateListener listener(composer);
        composer.cellExtrasChangedListeners.pushBack(&listener);
        auto* first = reinterpret_cast<CellExtraStore*>(uintptr_t(1));
        auto* second = reinterpret_cast<CellExtraStore*>(uintptr_t(2));

        composer.setCellExtras(first);

        STD_INSIST(listener.calls == 1);
        STD_INSIST(listener.extras == first);

        composer.setCellExtras(first);

        STD_INSIST(listener.calls == 1);

        composer.setCellExtras(second);

        STD_INSIST(listener.calls == 2);
        STD_INSIST(listener.extras == second);
    }

    STD_TEST(ListenerMayRemoveItselfDuringPublication) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        RemovingListener removing;
        StateListener trailing(composer);
        composer.contentScaleChangedListeners.pushBack(&removing);
        composer.contentScaleChangedListeners.pushBack(&trailing);

        composer.setContentScale(1.25f);
        composer.setContentScale(1.5f);

        STD_INSIST(removing.calls == 1);
        STD_INSIST(trailing.calls == 2);
    }

    // A1: contentInsets() is the only layout-facing source of geometry,
    // and what it reports on a side is the user's border plus whatever
    // chrome reserves there. The border half of that sum holds in every
    // build, before and after T5 and T6 add reserves, so it is asserted
    // as a lower bound rather than as an equality.
    STD_TEST(ContentInsetsCarryTheBorderOnEverySide) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        options.border = 7;
        composer.opts = &options;

        const Insets atOneX = composer.contentInsets();

        STD_INSIST(composer.borderPixels() == 7);
        STD_INSIST(atOneX.left >= composer.borderPixels());
        STD_INSIST(atOneX.top >= composer.borderPixels());
        STD_INSIST(atOneX.right >= composer.borderPixels());
        STD_INSIST(atOneX.bottom >= composer.borderPixels());

        // The reserve is in backing pixels: a 1.5x display scales the
        // border with everything else, and the insets follow it.
        composer.setContentScale(1.5f);
        const Insets atOneAndAHalf = composer.contentInsets();

        STD_INSIST(composer.borderPixels() == 11);
        STD_INSIST(atOneAndAHalf.left >= composer.borderPixels());
        STD_INSIST(atOneAndAHalf.top >= composer.borderPixels());
        STD_INSIST(atOneAndAHalf.right >= composer.borderPixels());
        STD_INSIST(atOneAndAHalf.bottom >= composer.borderPixels());
        STD_INSIST(atOneAndAHalf.left > atOneX.left);
        STD_INSIST(atOneAndAHalf.top > atOneX.top);
    }

    // The other half of the same sum. This test was written by R3-test
    // as ChromeReservesAreStillZeroOnEverySide, a deliberate alarm: it
    // asserted that no reserve existed anywhere, so that the first task
    // to land one - T5, here - would be forced to come back and write
    // the asymmetric tests the plan asks for instead of quietly getting
    // a green run. T5 rewrote it into what the alarm was standing in
    // for: a window with nothing reserving anything still has four
    // sides that are exactly the border, and the sides that no chrome
    // claimed stay that way once one of them is claimed.
    STD_TEST(ChromeReservesAreZeroUntilSomethingClaimsASide) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        options.border = 7;
        composer.opts = &options;

        const Insets insets = composer.contentInsets();

        STD_INSIST(composer.chromeReserve(ChromeSide::Top) == 0);
        STD_INSIST(composer.chromeReserve(ChromeSide::Right) == 0);
        STD_INSIST(composer.chromeReserve(ChromeSide::Bottom) == 0);
        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 0);
        STD_INSIST(insets.left == composer.borderPixels());
        STD_INSIST(insets.top == composer.borderPixels());
        STD_INSIST(insets.right == composer.borderPixels());
        STD_INSIST(insets.bottom == composer.borderPixels());

        options.border = 0;

        STD_INSIST(composer.contentInsets().left == 0);
        STD_INSIST(composer.contentInsets().top == 0);
        STD_INSIST(composer.contentInsets().right == 0);
        STD_INSIST(composer.contentInsets().bottom == 0);

        // Claiming one side leaves the other three exactly where they
        // were: two pieces of chrome own two different edges (T5 the
        // right, T6 the top) and never see each other's call.
        composer.setChromeReserve(ChromeSide::Right, 220);

        STD_INSIST(composer.chromeReserve(ChromeSide::Right) == 220);
        STD_INSIST(composer.contentInsets().right == 220);
        STD_INSIST(composer.contentInsets().top == 0);
        STD_INSIST(composer.contentInsets().bottom == 0);
        STD_INSIST(composer.contentInsets().left == 0);

        // And giving it back is the whole of turning the chrome off.
        composer.setChromeReserve(ChromeSide::Right, 0);

        STD_INSIST(composer.contentInsets().right == 0);
    }

    // A1, the first asymmetric inset in the tree: the sidebar's width is
    // an option in *logical points* and the insets are in backing
    // pixels, so the reserve has to be scaled exactly as the border is.
    // Getting this wrong is invisible at 1x and hands back half the
    // reserve on a Retina display, which puts the text under the panel
    // and misses the hit-test by the same 220 points.
    STD_TEST(ContentInsetsReserveTheSidebarOnTheRightInBackingPixels) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        options.border = 7;
        composer.opts = &options;
        composer.setChromeReserve(ChromeSide::Right, 220);

        const Insets atOneX = composer.contentInsets();

        STD_INSIST(atOneX.right == 227);
        STD_INSIST(atOneX.top == 7);
        STD_INSIST(atOneX.bottom == 7);
        STD_INSIST(atOneX.left == 7);
        STD_INSIST(atOneX.right > atOneX.top);
        STD_INSIST(atOneX.right > atOneX.bottom);
        STD_INSIST(atOneX.right > atOneX.left);

        // Two backing pixels per point: the border doubles, and so must
        // the reserve sitting on top of it. A reserve stored in pixels
        // instead of points would still read 220 here.
        composer.setContentScale(2.0f);
        const Insets atTwoX = composer.contentInsets();

        STD_INSIST(atTwoX.top == 14);
        STD_INSIST(atTwoX.right == 454);
        STD_INSIST(atTwoX.right - atTwoX.top == 440);
        STD_INSIST(atTwoX.left == 14);
        STD_INSIST(atTwoX.bottom == 14);
    }

    // The sidebar takes its width out of the grid, not out of the
    // window: the surface is the same and the terminal is narrower by
    // exactly the reserve, which is what cmd+b is for (A7) and what the
    // shell hears about through the resize this publishes.
    STD_TEST(ResizeGivesTheSidebarItsColumnsBackWhenItIsHidden) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        StateListener listener(composer);
        composer.resizedListeners.pushBack(&listener);
        Options options;
        options.border = 0;
        composer.opts = &options;
        composer.setContentScale(2.0f);
        composer.setGlyphSize(8, 16);
        // A whole number of cells wide, so "fewer by the reserve" is an
        // exact statement rather than one rounded into invisibility.
        composer.resize(1600, 800);
        const u16 wide = composer.columns;

        STD_INSIST(wide == 200);

        composer.setChromeReserve(ChromeSide::Right, 220);

        // 220 points at 2x is 440 backing pixels is 55 columns of 8.
        STD_INSIST(composer.columns == wide - 55);
        STD_INSIST(composer.pixelWidth == 1600);
        // The rows are the sidebar's business on no axis at all.
        STD_INSIST(composer.rows == 50);
        // And the shell heard about it: setChromeReserve() publishes the
        // resize itself, so cmd+b needs no second mechanism to make the
        // pty follow.
        STD_INSIST(listener.calls == 2);
        STD_INSIST(listener.columns == wide - 55);

        composer.setChromeReserve(ChromeSide::Right, 0);

        STD_INSIST(composer.columns == wide);
        STD_INSIST(listener.calls == 3);
    }

    // The pointer's half of the same asymmetry (A1). It lives here
    // rather than in mouse_frontend_ut.cpp because what is under test is
    // a Composer whose four sides differ - mouseGeometry() is only the
    // shortest way to ask it where the content box ends.
    STD_TEST(PointerStopsAtTheSidebarsEdgeNotTheWindows) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        options.border = 0;
        composer.opts = &options;
        composer.setGlyphSize(8, 16);
        composer.setChromeReserve(ChromeSide::Right, 20);
        // 196 wide less the 20 the panel holds is 176: exactly 22 cells,
        // so the last column ends where the panel begins.
        composer.resize(196, 160);

        STD_INSIST(composer.columns == 22);

        const MouseGeometry geometry = mouseGeometry(composer);
        u16 column = 0;
        u16 row = 0;

        // The last pixel of the last column is still the terminal's.
        STD_INSIST(mouseCell(175, 0, geometry, column, row));
        STD_INSIST(column == 21);
        STD_INSIST(row == 0);

        // The first pixel of the panel is not, and neither is anything
        // right of it: a click on a tab must never also land in the
        // grid behind it.
        STD_INSIST(!mouseCell(176, 0, geometry, column, row));
        STD_INSIST(!mouseCell(195, 0, geometry, column, row));
        STD_INSIST(column == 21);

        // The left edge is untouched by a reserve on the right - the
        // side the panel is on is not a detail the mapping may guess.
        STD_INSIST(mouseCell(0, 0, geometry, column, row));
        STD_INSIST(column == 0);
    }

    // R4-test, debt of wave 3, item 1 - the vertical half. T5 proved the
    // sidebar's reserve is scaled on the way out of contentInsets(); the
    // strip T6 puts on the top edge is the first reserve that makes the
    // *vertical* pair differ, and it is the pair the row count and the
    // renderer's grid origin are both counted from. A reserve stored in
    // backing pixels rather than points reads the same at 1x and hands
    // back half the strip at 2x, which puts the first row of text under
    // the title bar on exactly the displays this terminal ships on.
    STD_TEST(ContentInsetsReserveTheTitleBarStripOnTopInBackingPixels) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        options.border = 7;
        composer.opts = &options;
        composer.setChromeReserve(ChromeSide::Top, 32);

        const Insets atOneX = composer.contentInsets();

        STD_INSIST(atOneX.top == 39);
        STD_INSIST(atOneX.bottom == 7);
        STD_INSIST(atOneX.left == 7);
        STD_INSIST(atOneX.right == 7);
        // The asymmetry the whole debt turns on: until this reserve
        // existed, left and top were the same number and no test could
        // tell one from the other.
        STD_INSIST(atOneX.top > atOneX.left);
        STD_INSIST(atOneX.top > atOneX.bottom);

        composer.setContentScale(2.0f);
        const Insets atTwoX = composer.contentInsets();

        STD_INSIST(atTwoX.top == 78);
        STD_INSIST(atTwoX.bottom == 14);
        STD_INSIST(atTwoX.left == 14);
        STD_INSIST(atTwoX.right == 14);
        STD_INSIST(atTwoX.top - atTwoX.left == 64);
    }

    // A1: four sides, four owners, four numbers. Nothing in the tree
    // sets the bottom edge today, which is exactly why a Top/Bottom
    // transposition inside contentInsets() survives every other test
    // here - both reserves read zero on one of the two ends. Naming
    // four distinct reserves and reading four distinct insets is what
    // pins the mapping from ChromeSide to Insets field rather than the
    // two entries production happens to use.
    STD_TEST(EachChromeSideKeepsItsOwnReserveOnItsOwnEdge) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        options.border = 1;
        composer.opts = &options;

        composer.setChromeReserve(ChromeSide::Top, 10);
        composer.setChromeReserve(ChromeSide::Right, 200);
        composer.setChromeReserve(ChromeSide::Bottom, 30);
        composer.setChromeReserve(ChromeSide::Left, 4000);

        STD_INSIST(composer.chromeReserve(ChromeSide::Top) == 10);
        STD_INSIST(composer.chromeReserve(ChromeSide::Right) == 200);
        STD_INSIST(composer.chromeReserve(ChromeSide::Bottom) == 30);
        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 4000);

        const Insets insets = composer.contentInsets();

        STD_INSIST(insets.top == 11);
        STD_INSIST(insets.right == 201);
        STD_INSIST(insets.bottom == 31);
        // A reserve past the option maximum is still carried through at
        // its own size: scaledPixels() saturates for arithmetic reasons
        // alone, far above anything an option can ask for (F4, Q1), and
        // the border is added on top of it either way.
        STD_INSIST(insets.left == 4001);

        // Releasing one side is the whole of turning that piece of
        // chrome off, and it says nothing about the other three.
        composer.setChromeReserve(ChromeSide::Right, 0);

        const Insets afterwards = composer.contentInsets();

        STD_INSIST(afterwards.right == 1);
        STD_INSIST(afterwards.top == 11);
        STD_INSIST(afterwards.bottom == 31);
        STD_INSIST(afterwards.left == 4001);
    }

    // F4, Q1: the reserve is what keeps text out from under the chrome,
    // and the chrome is drawn from the same option in points - so the
    // two agree only while the conversion between them is a pure scale.
    // It was not: the pixel ceiling froze the reserve at 3000 backing
    // pixels while -sidebarWidth is validated at 1..3000 *points*, so
    // from scale 2 upward the panel outgrew its own reserve without
    // bound. R4-qa measured the consequence on a 3456 px window: 1500,
    // 1600 and 2800 points of sidebar all left the same 24 columns while
    // the panel reached 5600 px, i.e. 1300 pt of terminal text drawn
    // underneath it. The whole legal range at the scales a display
    // actually reports is what this walks, because a ceiling is invisible
    // to any test that only samples below it.
    STD_TEST(EveryLegalReserveIsWorthItsOwnWidthInPixels) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        options.border = 0;
        composer.opts = &options;
        composer.setGlyphSize(8, 16);

        const float scales[] = {1.0f, 1.5f, 2.0f, 3.0f};
        for (const float scale : scales) {
            composer.setContentScale(scale);
            // 1..3000 is what options.cpp accepts for -sidebarWidth; the
            // step keeps the walk cheap without letting it skip the
            // region an old ceiling would have flattened.
            for (u16 points = 1; points <= 3000; points = (u16)(points + 7)) {
                composer.setChromeReserve(ChromeSide::Right, points);
                const u16 expected = (u16)(points * scale + 0.5f);
                STD_INSIST(composer.contentInsets().right == expected);
            }
        }

        // And the three widths R4-qa could not tell apart now cost three
        // different column counts on the window it measured.
        composer.setContentScale(2.0f);
        composer.setChromeReserve(ChromeSide::Right, 1500);
        composer.resize(3456, 1000);
        const u16 at1500 = composer.columns;
        composer.setChromeReserve(ChromeSide::Right, 1600);
        const u16 at1600 = composer.columns;

        STD_INSIST(at1500 == 57);
        STD_INSIST(at1600 == 32);
        // 2800 pt is 5600 px of panel on a 3456 px window: there is no
        // room left for text at all, and the grid says so instead of
        // handing the shell columns that are drawn under the panel.
        composer.setChromeReserve(ChromeSide::Right, 2800);

        STD_INSIST(composer.columns == 1);
    }

    // F4, Q2: T6's acceptance criterion was "no resize events in the
    // -verbose log while the pointer crosses the strip", and it could
    // not have caught one. The trace lived on the platform's window
    // callback, while a reserve re-counts the grid straight through
    // Composer::resize() - so R4-qa's cmd+b moved the grid three times
    // and printed nothing, and the criterion read green either way. The
    // two halves of the instrument are here: a grid that changed must
    // print, and a grid that did not must stay silent, or the next wave
    // reads "no lines" as "nothing moved" again.
    STD_TEST(EveryGridChangePrintsAndNothingElseDoes) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        options.border = 0;
        options.verbose = true;
        composer.opts = &options;
        composer.setGlyphSize(8, 16);
        composer.resize(800, 400);

        const u16 rows = composer.rows;
        char log[512];

        StderrCapture reserved;
        // No platform callback anywhere in this: exactly the path cmd+b
        // and the title bar strip take.
        composer.setChromeReserve(ChromeSide::Top, 32);
        const size_t printed = reserved.restore(log, sizeof(log));

        STD_INSIST(composer.rows == rows - 2);
        STD_INSIST(printed > 0);
        STD_INSIST(strstr(log, "window:") != nullptr);
        STD_INSIST(strstr(log, "grid 100x25 -> 100x23") != nullptr);

        StderrCapture unchanged;
        // The hover case A7 is about: the same reserve set again, and
        // the same window size delivered again. Nothing moved, so
        // nothing is written - which is what makes a line meaningful.
        composer.setChromeReserve(ChromeSide::Top, 32);
        composer.resize(800, 400);
        const size_t silent = unchanged.restore(log, sizeof(log));

        STD_INSIST(silent == 0);
    }

    // R4-test, debt item 4, the vertical half: the rows come out of the
    // top and bottom *sum*, not out of twice one border. A resize that
    // went back to the scalar borderPixels() (mutation R6 of R3-test,
    // provably equivalent before this wave) now loses the strip and
    // hands the shell rows that are not on the screen.
    STD_TEST(ResizeTakesTheStripOutOfTheRowsAndLeavesTheColumnsAlone) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        StateListener listener(composer);
        composer.resizedListeners.pushBack(&listener);
        Options options;
        options.border = 0;
        composer.opts = &options;
        composer.setContentScale(2.0f);
        composer.setGlyphSize(8, 16);
        composer.resize(1600, 800);
        const u16 tall = composer.rows;

        STD_INSIST(tall == 50);
        STD_INSIST(composer.columns == 200);

        composer.setChromeReserve(ChromeSide::Top, 32);

        // 32 points at 2x is 64 backing pixels is 4 rows of 16.
        STD_INSIST(composer.rows == tall - 4);
        STD_INSIST(composer.columns == 200);
        STD_INSIST(composer.pixelHeight == 800);
        STD_INSIST(listener.calls == 2);
        STD_INSIST(listener.rows == tall - 4);

        // The two vertical reserves add up rather than replacing one
        // another: a second one on the opposite edge costs its own rows.
        composer.setChromeReserve(ChromeSide::Bottom, 32);

        STD_INSIST(composer.rows == tall - 8);
        STD_INSIST(composer.columns == 200);

        composer.setChromeReserve(ChromeSide::Top, 0);
        composer.setChromeReserve(ChromeSide::Bottom, 0);

        STD_INSIST(composer.rows == tall);
    }

    // The documented no-op: setting a side to the number it already
    // holds must not re-count the grid and must not publish a resize.
    // The sidebar and the strip both re-apply their reserve from the
    // current config on every reload, so a reserve that republished on
    // every call would send the shell a SIGWINCH per SIGUSR1 - and, in
    // the hover path A7 forbids, per pointer crossing.
    STD_TEST(SettingTheSameReserveTwiceLeavesTheGridUntouched) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        StateListener listener(composer);
        composer.resizedListeners.pushBack(&listener);
        Options options;
        options.border = 0;
        composer.opts = &options;
        composer.setGlyphSize(8, 16);
        composer.resize(800, 400);

        composer.setChromeReserve(ChromeSide::Top, 32);

        const size_t published = listener.calls;
        const u16 rows = composer.rows;

        for (int again = 0; again < 10; ++again) {
            composer.setChromeReserve(ChromeSide::Top, 32);
        }

        STD_INSIST(composer.rows == rows);
        STD_INSIST(listener.calls == published);
    }

    // R4-test, debt item 3: the pointer mapping driven off a Composer
    // whose four sides all differ. mouse_frontend_ut.cpp sweeps the free
    // functions with an Insets it builds by hand; what was never checked
    // is the bridge - mouseGeometry(Composer&) - because until this wave
    // there was no way to hand a Composer an asymmetric content box.
    // Every pixel of the surface is asked, so a side paired with the
    // wrong axis, or the bridge falling back to the scalar
    // borderPixels() (mutation R5 of R3-test), is a wrong answer on a
    // named pixel rather than an unobservable transposition.
    STD_TEST(EveryPixelOfAComposersContentBoxAnswersFromItsOwnSide) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        options.border = 3;
        composer.opts = &options;
        composer.setContentScale(2.0f);
        composer.setGlyphSize(8, 16);
        composer.setChromeReserve(ChromeSide::Top, 10);
        composer.setChromeReserve(ChromeSide::Right, 20);
        composer.setChromeReserve(ChromeSide::Bottom, 5);
        composer.resize(132, 122);

        const Insets insets = composer.contentInsets();
        // Four different numbers, so no two sides can stand in for each
        // other anywhere below.
        STD_INSIST(insets.left == 6);
        STD_INSIST(insets.top == 26);
        STD_INSIST(insets.right == 46);
        STD_INSIST(insets.bottom == 16);
        STD_INSIST(composer.columns == 10);
        STD_INSIST(composer.rows == 5);

        const MouseGeometry geometry = mouseGeometry(composer);

        STD_INSIST(geometry.framebufferWidth == 132);
        STD_INSIST(geometry.framebufferHeight == 122);
        STD_INSIST(geometry.glyphWidth == 8);
        STD_INSIST(geometry.glyphHeight == 16);

        for (int y = 0; y < 122; ++y) {
            for (int x = 0; x < 132; ++x) {
                const bool inside = x >= 6 && x < 132 - 46 && y >= 26 && y < 122 - 16;
                u16 column = 0xffff;
                u16 row = 0xffff;

                STD_INSIST(mouseCell(x, y, geometry, column, row) == inside);

                if (!inside) {
                    STD_INSIST(column == 0xffff);
                    STD_INSIST(row == 0xffff);
                    continue;
                }
                STD_INSIST(column == (u16)((x - 6) / 8));
                STD_INSIST(row == (u16)((y - 26) / 16));
                STD_INSIST(column < composer.columns);
                STD_INSIST(row < composer.rows);
            }
        }

        // The scroll-on-drag direction reads the same two vertical
        // sides, and the strip is the top one: a drag held on the title
        // bar scrolls up, not nowhere.
        STD_INSIST(mouseAutoscrollDirection(25, geometry) == -1);
        STD_INSIST(mouseAutoscrollDirection(26, geometry) == -1);
        STD_INSIST(mouseAutoscrollDirection(27, geometry) == 0);
        STD_INSIST(mouseAutoscrollDirection(104, geometry) == 0);
        STD_INSIST(mouseAutoscrollDirection(105, geometry) == 1);
        STD_INSIST(mouseAutoscrollDirection(121, geometry) == 1);

        // And what the program in the terminal is told: cell 1,1 is the
        // pixel the content box starts on, on both axes at once.
        const MouseProtocolPoint home = mouseProtocolPoint(MouseTrackingEnc::Default, 6, 26, geometry);
        STD_INSIST(home.column == 1);
        STD_INSIST(home.row == 1);
        const MouseProtocolPoint last = mouseProtocolPoint(MouseTrackingEnc::Default, 85, 105, geometry);
        STD_INSIST(last.column == 10);
        STD_INSIST(last.row == 5);
    }

    // resize() and contentInsets() are the two directions of one
    // formula: the grid a surface reports has to be the grid that
    // surface's content box holds, at every size including the ones
    // where the box ends mid-cell or does not exist at all.
    STD_TEST(ResizeCountsTheGridOutOfTheContentInsets) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        Options options;
        options.border = 7;
        composer.opts = &options;
        composer.setContentScale(1.5f);
        composer.setGlyphSize(8, 16);

        const Insets insets = composer.contentInsets();

        for (u16 pixels = 1; pixels < 600; pixels = (u16)(pixels + 7)) {
            composer.resize(pixels, pixels);

            STD_INSIST(composer.columns == gridColumns(pixels, insets, composer.glyphWidth));
            STD_INSIST(composer.rows == gridRows(pixels, insets, composer.glyphHeight));
        }

        // A surface smaller than its own reserve still reports a usable
        // grid rather than a zero-column terminal.
        composer.resize(1, 1);

        STD_INSIST(composer.columns == 1);
        STD_INSIST(composer.rows == 1);

        // And a surface sized for N cells reports exactly N back.
        const u16 width = (u16)(gridPixelWidth(37, insets, composer.glyphWidth));
        const u16 height = (u16)(gridPixelHeight(11, insets, composer.glyphHeight));
        composer.resize(width, height);

        STD_INSIST(composer.columns == 37);
        STD_INSIST(composer.rows == 11);
    }
}
