/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "grapheme.h"

#include <std/tst/ut.h>

using namespace stl;

STD_TEST_SUITE(Grapheme) {
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

    STD_TEST(KeepsIndependentClustersIndependent) {
        static constexpr u32 input[] = {0x65e5, 0x672c, 0x8a9e, 0x1f1fa, 0x1f1f8, 0xac00, 0xac01};
        static constexpr bool boundaries[] = {true, true, true, true, false, true, true};
        GraphemeBreaker breaker;

        for (size_t index = 0; index < sizeof(input) / sizeof(input[0]); ++index) {
            STD_INSIST(breaker.breakBefore(input[index]) == boundaries[index]);
        }
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
