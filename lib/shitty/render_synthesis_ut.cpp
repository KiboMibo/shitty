/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "render_synthesis.h"

#include <math.h>

#include <std/tst/ut.h>

using namespace stl;

namespace {
    static float area(u32 codepoint, int width, int height, float stroke) {
        float result = 0.0f;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                result += synthesizedCoverage(codepoint, x, y, width, height, stroke);
            }
        }
        return result;
    }

    static float rowMass(u32 codepoint, int y, int width, int height, float stroke) {
        float result = 0.0f;
        for (int x = 0; x < width; ++x) {
            result += synthesizedCoverage(codepoint, x, y, width, height, stroke);
        }
        return result / width;
    }

    static float columnMass(u32 codepoint, int x, int width, int height, float stroke) {
        float result = 0.0f;
        for (int y = 0; y < height; ++y) {
            result += synthesizedCoverage(codepoint, x, y, width, height, stroke);
        }
        return result / height;
    }

    static void near(float actual, float expected) {
        STD_INSIST(fabsf(actual - expected) < 1e-4f);
    }
}

STD_TEST_SUITE(RenderSynthesis) {
    STD_TEST(PreservesFractionalLightAndHeavyInk) {
        static constexpr int width = 12;
        static constexpr int height = 24;
        static constexpr float stroke = 1.5f;

        near(area(0x2500, width, height, stroke), width * stroke);
        near(area(0x2501, width, height, stroke), width * stroke * 2.0f);
        near(area(0x2502, width, height, stroke), height * stroke);
        near(area(0x2503, width, height, stroke), height * stroke * 2.0f);
        near(area(0x2550, width, height, stroke), width * stroke * 2.0f);
        near(area(0x2551, width, height, stroke), height * stroke * 2.0f);
        near(area(0x2500, width, height, 0.75f), width * 0.75f);
    }

    STD_TEST(DoubleLinesUseEqualStrokeAndGap) {
        static constexpr int width = 12;
        static constexpr int height = 24;
        static constexpr float stroke = 2.0f;

        near(area(0x2550, width, height, stroke), width * stroke * 2.0f);
        near(rowMass(0x2550, 9, width, height, stroke), 1.0f);
        near(rowMass(0x2550, 10, width, height, stroke), 1.0f);
        near(rowMass(0x2550, 11, width, height, stroke), 0.0f);
        near(rowMass(0x2550, 12, width, height, stroke), 0.0f);
        near(rowMass(0x2550, 13, width, height, stroke), 1.0f);
        near(rowMass(0x2550, 14, width, height, stroke), 1.0f);
    }

    STD_TEST(DoubleJunctionRoutesRailsAroundItsCenter) {
        static constexpr int width = 12;
        static constexpr int height = 24;
        static constexpr float stroke = 2.0f;

        // ╬ bends each incoming rail into its adjacent horizontal rail and
        // keeps the central gap empty.
        near(synthesizedCoverage(0x256c, 4, 10, width, height, stroke), 1.0f);
        near(synthesizedCoverage(0x256c, 5, 11, width, height, stroke), 0.0f);

        // ╪ and ╫ preserve the single stroke through the center while the
        // perpendicular direction remains double.
        near(synthesizedCoverage(0x256a, 5, 11, width, height, stroke), 1.0f);
        near(synthesizedCoverage(0x256b, 5, 11, width, height, stroke), 1.0f);
    }

    STD_TEST(ScanLinesAndDentistryUseTheSharedFractionalStroke) {
        static constexpr int width = 12;
        static constexpr int height = 24;
        static constexpr float stroke = 1.5f;

        near(area(0x23ba, width, height, stroke), width * stroke);
        const float bracket = width * stroke + height * stroke - stroke * stroke;
        near(area(0x23be, width, height, stroke), bracket);
    }

    STD_TEST(DashesKeepTheSolidLinePixelPhase) {
        static constexpr int width = 12;
        static constexpr int height = 24;
        static constexpr float stroke = 1.5f;

        for (int y = 0; y < height; ++y) {
            const float solid = synthesizedCoverage(0x2501, 0, y, width, height, stroke);
            const float dashed = synthesizedCoverage(0x2505, 0, y, width, height, stroke);
            near(dashed, solid);
        }
        for (int x = 0; x < width; ++x) {
            const float solid = synthesizedCoverage(0x2503, x, 0, width, height, stroke);
            const float dashed = synthesizedCoverage(0x2507, x, 0, width, height, stroke);
            near(dashed, solid);
        }
    }

    STD_TEST(DiagonalsAndArcsRespectTheSharedStroke) {
        static constexpr int width = 12;
        static constexpr int height = 24;
        static constexpr float thin = 1.25f;
        static constexpr float thick = 2.25f;

        STD_INSIST(area(0x2571, width, height, thick) > area(0x2571, width, height, thin));
        STD_INSIST(area(0x256d, width, height, thick) > area(0x256d, width, height, thin));
        for (int y = 0; y < height; ++y) {
            near(
                synthesizedCoverage(0x2571, 3, y, width, height, thin),
                synthesizedCoverage(0x2572, width - 4, y, width, height, thin)
            );
        }
    }

    STD_TEST(ArcsAreCircularMirroredAndJoinStraightTails) {
        static constexpr int width = 12;
        static constexpr int height = 24;
        static constexpr float stroke = 2.0f;

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const float downRight = synthesizedCoverage(0x256d, x, y, width, height, stroke);
                near(downRight, synthesizedCoverage(0x256e, width - 1 - x, y, width, height, stroke));
                near(downRight, synthesizedCoverage(0x2570, x, height - 1 - y, width, height, stroke));
                near(downRight, synthesizedCoverage(0x256f, width - 1 - x, height - 1 - y, width, height, stroke));
            }
        }

        // The cell is twice as tall as it is wide. The bend stays circular
        // on the short side and continues as a straight vertical stem,
        // instead of stretching into a half-cell ellipse.
        for (int x = 0; x < width; ++x) {
            near(
                synthesizedCoverage(0x256d, x, height - 1, width, height, stroke),
                synthesizedCoverage(0x2502, x, height - 1, width, height, stroke)
            );
        }

        static constexpr int wideWidth = 24;
        static constexpr int wideHeight = 12;
        for (int y = 0; y < wideHeight; ++y) {
            near(
                synthesizedCoverage(0x256d, wideWidth - 1, y, wideWidth, wideHeight, stroke),
                synthesizedCoverage(0x2500, wideWidth - 1, y, wideWidth, wideHeight, stroke)
            );
        }
    }

    STD_TEST(EveryBoxDrawingCharacterHasInkAndBoundedCoverage) {
        static constexpr int width = 12;
        static constexpr int height = 24;
        static constexpr float stroke = 1.5f;

        for (u32 codepoint = 0x2500; codepoint <= 0x257f; ++codepoint) {
            float ink = 0.0f;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const float coverage = synthesizedCoverage(codepoint, x, y, width, height, stroke);
                    STD_INSIST(coverage >= 0.0f && coverage <= 1.0f);
                    ink += coverage;
                }
            }
            STD_INSIST(ink > 0.0f);
        }

        // The vertical double glyph mirrors its horizontal counterpart's
        // two-stroke cross section, despite the rectangular cell.
        for (int offset = -6; offset < 6; ++offset) {
            near(
                rowMass(0x2550, height / 2 + offset, width, height, 2.0f),
                columnMass(0x2551, width / 2 + offset, width, height, 2.0f)
            );
        }
    }
}
