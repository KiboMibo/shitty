/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "render_arena.h"

#include <std/tst/ut.h>

using namespace stl;

namespace {
    // Two panes are two screens; only their addresses matter here. The
    // values differ so that nothing may fold three identical constants
    // into one object and hand out one address for all of them.
    static const int paneA = 1;
    static const int paneB = 2;
    static const int paneC = 3;

    struct Plan {
        PaneArenaCopy copies[4];

        void run(PaneArenaMirror& mirror, const PaneArenaRequest* panes, size_t count) {
            for (PaneArenaCopy& copy : copies) {
                copy = {};
            }
            mirror.plan(panes, count, copies);
        }

        bool sends(size_t index, size_t base, size_t from, size_t to) const {
            return copies[index].base == base && copies[index].from == from && copies[index].to == to;
        }

        bool sendsNothing(size_t index, size_t base) const {
            return copies[index].base == base && copies[index].from == copies[index].to;
        }
    };
}

STD_TEST_SUITE(PaneArenaMirror) {
    // The single-pane path is the one the README's numbers are measured
    // on: one pane must still send its tail and nothing else.
    STD_TEST(SendsOnePaneTailOnly) {
        PaneArenaMirror mirror;
        Plan plan;
        PaneArenaRequest panes[1] = {{&paneA, 3, 100}};

        plan.run(mirror, panes, 1);
        STD_INSIST(plan.sends(0, 0, 0, 100));
        STD_INSIST(mirror.used() == 100);

        panes[0].used = 140;
        plan.run(mirror, panes, 1);
        STD_INSIST(plan.sends(0, 0, 100, 140));

        plan.run(mirror, panes, 1);
        STD_INSIST(plan.sendsNothing(0, 0));
    }

    // A new generation means the pane's own strips moved wholesale.
    STD_TEST(SendsAPaneWholeOnANewGeneration) {
        PaneArenaMirror mirror;
        Plan plan;
        PaneArenaRequest panes[1] = {{&paneA, 3, 100}};

        plan.run(mirror, panes, 1);
        panes[0].generation = 4;
        plan.run(mirror, panes, 1);

        STD_INSIST(plan.sends(0, 0, 0, 100));
    }

    // A pane that shrank cannot owe a tail that starts past its end.
    STD_TEST(ClampsTheTailToAShrunkPane) {
        PaneArenaMirror mirror;
        Plan plan;
        PaneArenaRequest panes[1] = {{&paneA, 3, 100}};

        plan.run(mirror, panes, 1);
        panes[0].used = 40;
        plan.run(mirror, panes, 1);

        STD_INSIST(plan.sendsNothing(0, 0));
        STD_INSIST(mirror.used() == 40);
    }

    STD_TEST(LaysPanesOutBackToBackAndKeepsThem) {
        PaneArenaMirror mirror;
        Plan plan;
        const PaneArenaRequest panes[2] = {{&paneA, 3, 100}, {&paneB, 9, 60}};

        plan.run(mirror, panes, 2);
        STD_INSIST(plan.sends(0, 0, 0, 100));
        STD_INSIST(plan.sends(1, 100, 0, 60));
        STD_INSIST(mirror.used() == 160);

        plan.run(mirror, panes, 2);
        STD_INSIST(plan.sendsNothing(0, 0));
        STD_INSIST(plan.sendsNothing(1, 100));
    }

    // A3, the whole point: the generation is compared next to the pane
    // that produced it. These two panes report the same generation
    // number from two different screens, and the frame that swaps their
    // order puts each of them where the other one's bytes are - which
    // only the identity in the comparison can tell apart. Compare the
    // generations alone (or match panes by their position in the frame)
    // and both of these read as "already mirrored", leaving each pane
    // drawn from the other's glyphs.
    STD_TEST(SwappedPanesSendBothAgain) {
        PaneArenaMirror mirror;
        Plan plan;
        const PaneArenaRequest forward[2] = {{&paneA, 7, 100}, {&paneB, 7, 60}};
        const PaneArenaRequest swapped[2] = {{&paneB, 7, 60}, {&paneA, 7, 100}};

        plan.run(mirror, forward, 2);
        plan.run(mirror, swapped, 2);

        STD_INSIST(plan.sends(0, 0, 0, 60));
        STD_INSIST(plan.sends(1, 60, 0, 100));
    }

    // Same generation number, same place in the frame, different pane:
    // a split that replaced the right-hand terminal.
    STD_TEST(SendsAReplacedPaneWhole) {
        PaneArenaMirror mirror;
        Plan plan;
        const PaneArenaRequest before[2] = {{&paneA, 7, 100}, {&paneB, 7, 60}};
        const PaneArenaRequest after[2] = {{&paneA, 7, 100}, {&paneC, 7, 60}};

        plan.run(mirror, before, 2);
        plan.run(mirror, after, 2);

        STD_INSIST(plan.sendsNothing(0, 0));
        STD_INSIST(plan.sends(1, 100, 0, 60));
    }

    // The first pane grew, so the second one's bytes are somewhere else
    // now: its own generation says nothing moved, and it did.
    STD_TEST(SendsAMovedPaneWhole) {
        PaneArenaMirror mirror;
        Plan plan;
        const PaneArenaRequest before[2] = {{&paneA, 7, 100}, {&paneB, 9, 60}};
        const PaneArenaRequest after[2] = {{&paneA, 7, 180}, {&paneB, 9, 60}};

        plan.run(mirror, before, 2);
        plan.run(mirror, after, 2);

        STD_INSIST(plan.sends(0, 0, 100, 180));
        STD_INSIST(plan.sends(1, 180, 0, 60));
    }

    // A pane that leaves the frame takes its range with it.
    STD_TEST(ForgetsAPaneThatLeftTheFrame) {
        PaneArenaMirror mirror;
        Plan plan;
        const PaneArenaRequest both[2] = {{&paneA, 7, 100}, {&paneB, 9, 60}};
        const PaneArenaRequest alone[1] = {{&paneB, 9, 60}};

        plan.run(mirror, both, 2);
        plan.run(mirror, alone, 1);
        STD_INSIST(plan.sends(0, 0, 0, 60));
        STD_INSIST(mirror.used() == 60);

        plan.run(mirror, both, 2);
        STD_INSIST(plan.sends(0, 0, 0, 100));
        STD_INSIST(plan.sends(1, 100, 0, 60));
    }

    STD_TEST(ResetForgetsTheMirror) {
        PaneArenaMirror mirror;
        Plan plan;
        const PaneArenaRequest panes[1] = {{&paneA, 3, 100}};

        plan.run(mirror, panes, 1);
        mirror.reset();
        STD_INSIST(mirror.used() == 0);

        plan.run(mirror, panes, 1);
        STD_INSIST(plan.sends(0, 0, 0, 100));
    }

    // A pane with no screen has no strips; it must not match the next
    // frame's pane that also has none.
    STD_TEST(NeverMatchesAPaneWithoutAScreen) {
        PaneArenaMirror mirror;
        Plan plan;
        const PaneArenaRequest panes[1] = {{nullptr, 0, 0}};

        plan.run(mirror, panes, 1);
        plan.run(mirror, panes, 1);

        STD_INSIST(plan.sendsNothing(0, 0));
        STD_INSIST(mirror.used() == 0);
    }
}
