/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "grapheme.h"

#include <std/tst/ut.h>

using namespace stl;

STD_TEST_SUITE(Grapheme) {
    STD_TEST(ReportsRepresentativeCodepointWidths) {
        STD_INSIST(codepointWidth('A') == 1);
        STD_INSIST(codepointWidth(0x0301) == 0);
        STD_INSIST(codepointWidth(0x1160) == 0);
        STD_INSIST(codepointWidth(0x11ff) == 0);
        STD_INSIST(codepointWidth(0x4e00) == 2);
        STD_INSIST(codepointWidth(0x1f1fa) == 2);
        STD_INSIST(codepointWidth(0) == 0);
    }

    STD_TEST(KeepsTerminalFormatControlsZeroWidth) {
        constexpr u32 controls[] = {
            0x0600,
            0x0605,
            0x06dd,
            0x070f,
            0x0890,
            0x0891,
            0x08e2,
            0x110bd,
            0x110cd,
        };
        for (const u32 codepoint : controls) {
            STD_INSIST(codepointWidth(codepoint) == 0);
        }
    }

    STD_TEST(AppliesVariationSelectorWidthOverrides) {
        STD_INSIST(graphemeWidthEffect(0x00a9, 0xfe0f) == GraphemeWidthEffect::Wide);
        STD_INSIST(graphemeWidthEffect(0x231a, 0xfe0e) == GraphemeWidthEffect::Narrow);
        STD_INSIST(graphemeWidthEffect('A', 0xfe0f) == GraphemeWidthEffect::Unchanged);
    }

    STD_TEST(DistinguishesSpacingMarksAndViramas) {
        STD_INSIST(graphemeWidthEffect(0x0915, 0x093e) == GraphemeWidthEffect::Wide);
        STD_INSIST(graphemeWidthEffect(0x0915, 0x094d) == GraphemeWidthEffect::Unchanged);
        STD_INSIST(graphemeWidthEffect('a', 0x0301) == GraphemeWidthEffect::Unchanged);
    }

    STD_TEST(BreaksEveryAsciiCodepoint) {
        GraphemeBreaker breaker;

        STD_INSIST(breaker.breakBefore('a'));
        STD_INSIST(breaker.breakBefore('b'));
        STD_INSIST(breaker.breakBefore(' '));
    }

    STD_TEST(KeepsCombiningSequenceTogether) {
        GraphemeBreaker breaker;

        STD_INSIST(breaker.breakBefore('a'));
        STD_INSIST(!breaker.breakBefore(0x0301));
        STD_INSIST(breaker.breakBefore('b'));
    }

    STD_TEST(KeepsEmojiZwjSequenceTogether) {
        GraphemeBreaker breaker;

        STD_INSIST(breaker.breakBefore(0x1f469));
        STD_INSIST(!breaker.breakBefore(0x200d));
        STD_INSIST(!breaker.breakBefore(0x1f4bb));
        STD_INSIST(breaker.breakBefore('x'));
    }

    STD_TEST(PairsRegionalIndicators) {
        GraphemeBreaker breaker;

        STD_INSIST(breaker.breakBefore(0x1f1fa));
        STD_INSIST(!breaker.breakBefore(0x1f1f8));
        STD_INSIST(breaker.breakBefore(0x1f1e8));
        STD_INSIST(!breaker.breakBefore(0x1f1e6));
    }

    STD_TEST(ResetStartsFreshBoundary) {
        GraphemeBreaker breaker;
        breaker.breakBefore('a');
        breaker.breakBefore(0x0301);
        breaker.reset();

        STD_INSIST(breaker.breakBefore(0x0301));
    }

    STD_TEST(SetBoundarySeedsPreviousCodepoint) {
        GraphemeBreaker breaker;
        breaker.setBoundaryAfter('a');

        STD_INSIST(!breaker.breakBefore(0x0301));
        STD_INSIST(breaker.breakBefore('b'));
    }
}
