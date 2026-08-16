/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "render_damage.h"

#include <std/tst/ut.h>

using namespace stl;

STD_TEST_SUITE(RenderDamage) {
    STD_TEST(MergesAdjacentRangesWithinGeneration) {
        RenderDamage::Entry entries[4]{};
        RenderDamage damage;
        damage.configure(entries, 4);
        damage.advance();
        damage.add(10, 2);
        damage.add(12, 3);

        STD_INSIST(damage.count == 1);
        STD_INSIST(damage.entry(0).begin == 10);
        STD_INSIST(damage.entry(0).count == 5);
    }

    STD_TEST(KeepsGenerationsSeparate) {
        RenderDamage::Entry entries[4]{};
        RenderDamage damage;
        damage.configure(entries, 4);
        damage.advance();
        damage.add(10, 2);
        damage.advance();
        damage.add(12, 3);

        STD_INSIST(damage.count == 2);
        STD_INSIST(damage.entry(0).generation == 1);
        STD_INSIST(damage.entry(1).generation == 2);
    }

    STD_TEST(ReusesCollectedRingSlots) {
        RenderDamage::Entry entries[2]{};
        RenderDamage damage;
        damage.configure(entries, 2);
        damage.advance();
        damage.add(1, 1);
        damage.advance();
        damage.add(2, 1);
        damage.collect(1);
        damage.advance();
        damage.add(3, 1);

        STD_INSIST(damage.count == 2);
        STD_INSIST(damage.entry(0).begin == 2);
        STD_INSIST(damage.entry(1).begin == 3);
    }

    STD_TEST(OverflowPromotesCurrentStateToFull) {
        RenderDamage::Entry entries[2]{};
        RenderDamage damage;
        damage.configure(entries, 2);
        damage.advance();
        damage.add(1, 1);
        damage.add(3, 1);
        damage.add(5, 1);

        STD_INSIST(damage.count == 0);
        STD_INSIST(damage.requiresFull(0, true));
        STD_INSIST(!damage.requiresFull(1, true));
    }

    STD_TEST(FullDropsEarlierJournal) {
        RenderDamage::Entry entries[4]{};
        RenderDamage damage;
        damage.configure(entries, 4);
        damage.advance();
        damage.add(1, 1);
        damage.advance();
        damage.full();
        damage.advance();
        damage.add(3, 1);

        STD_INSIST(damage.count == 1);
        STD_INSIST(damage.entry(0).begin == 3);
        STD_INSIST(damage.requiresFull(1, true));
        STD_INSIST(!damage.requiresFull(2, true));
    }

    STD_TEST(UninitializedImageAlwaysRequiresFull) {
        RenderDamage::Entry entries[1]{};
        RenderDamage damage;
        damage.configure(entries, 1);

        STD_INSIST(damage.requiresFull(damage.generation, false));
    }

    STD_TEST(CollectionRewindsEmptyJournal) {
        RenderDamage::Entry entries[2]{};
        RenderDamage damage;
        damage.configure(entries, 2);
        damage.advance();
        damage.add(1, 1);
        damage.collect(1);

        STD_INSIST(damage.count == 0);
        STD_INSIST(damage.begin == 0);
    }
}
