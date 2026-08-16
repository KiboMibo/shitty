/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "render_synthesis.h"

#include <math.h>

namespace {

    // Mirrors boxDrawingData in render.comp.
    static const u32 boxDrawingData[128] = {0x0005, 0x000a, 0x0050, 0x00a0, 0x6005, 0x600a, 0x6050, 0x60a0, 0x8005, 0x800a, 0x8050, 0x80a0, 0x0044, 0x0048, 0x0084, 0x0088, 0x0041, 0x0042, 0x0081, 0x0082, 0x0014, 0x0018, 0x0024, 0x0028, 0x0011, 0x0012, 0x0021, 0x0022, 0x0054, 0x0058, 0x0064, 0x0094, 0x00a4, 0x0068, 0x0098, 0x00a8, 0x0051, 0x0052, 0x0061, 0x0091, 0x00a1, 0x0062, 0x0092, 0x00a2, 0x0045, 0x0046, 0x0049, 0x004a, 0x0085, 0x0086, 0x0089, 0x008a, 0x0015, 0x0016, 0x0019, 0x001a, 0x0025, 0x0026, 0x0029, 0x002a, 0x0055, 0x0056, 0x0059, 0x005a, 0x0065, 0x0095, 0x00a5, 0x0066, 0x0069, 0x0096, 0x0099, 0x006a, 0x009a, 0x00a6, 0x00a9, 0x00aa, 0x4005, 0x400a, 0x4050, 0x40a0, 0x000f, 0x00f0, 0x004c, 0x00c4, 0x00cc, 0x0043, 0x00c1, 0x00c3, 0x001c, 0x0034, 0x003c, 0x0013, 0x0031, 0x0033, 0x005c, 0x00f4, 0x00fc, 0x0053, 0x00f1, 0x00f3, 0x004f, 0x00c5, 0x00cf, 0x001f, 0x0035, 0x003f, 0x005f, 0x00f5, 0x00ff, 0x0444, 0x0441, 0x0411, 0x0414, 0x0800, 0x1000, 0x1800, 0x0001, 0x0010, 0x0004, 0x0040, 0x0002, 0x0020, 0x0008, 0x0080, 0x0009, 0x0090, 0x0006, 0x0060};

    static u32 field(u32 data, int offset, int bits) {
        return (data >> offset) & ((1u << bits) - 1u);
    }

    static int minimum(int left, int right) {
        return left < right ? left : right;
    }

    static int maximum(int left, int right) {
        return left > right ? left : right;
    }

    static float smoothStep(float edge0, float edge1, float value) {
        const float t = (value - edge0) / (edge1 - edge0);
        const float clamped = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
        return clamped * clamped * (3.0f - 2.0f * clamped);
    }

    static float clampUnit(float value) {
        return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
    }

    // Exact one-dimensional pixel coverage of [first, last). Coordinates
    // live on pixel edges; coordinate names the pixel [coordinate, +1).
    static float intervalCoverage(int coordinate, float first, float last) {
        const float pixelFirst = (float)(coordinate);
        const float pixelLast = pixelFirst + 1.0f;
        const float overlapFirst = pixelFirst > first ? pixelFirst : first;
        const float overlapLast = pixelLast < last ? pixelLast : last;
        return clampUnit(overlapLast - overlapFirst);
    }

    static float rectangleCoverage(int pixelX, int pixelY, float left, float top, float right, float bottom) {
        if (right <= left || bottom <= top) {
            return 0.0f;
        }
        return intervalCoverage(pixelX, left, right) * intervalCoverage(pixelY, top, bottom);
    }

    static void addCoverage(float& coverage, float addition) {
        // Junction rectangles overlap deliberately. Keep their strongest
        // coverage instead of darkening a shared antialiased edge twice.
        coverage = coverage > addition ? coverage : addition;
    }

    static void addRectangle(float& coverage, int pixelX, int pixelY, float left, float top, float right, float bottom) {
        addCoverage(coverage, rectangleCoverage(pixelX, pixelY, left, top, right, bottom));
    }

    // Crisp integer stems need opposite pixel phases for odd and even
    // widths. Fractional stems inherit the phase of their nearest integer.
    static float strokeCenter(int size, float thickness) {
        const int rounded = (int)(thickness + 0.5f);
        return (float)(size / 2) + ((rounded & 1) != 0 ? 0.5f : 0.0f);
    }

    static bool inDash(int coordinate, int length, u32 count) {
        if (count == 0) {
            return true;
        }
        const int partCount = (int)(count * 2 - 1);
        const int part = coordinate * partCount / length;
        return (part & 1) == 0;
    }

    static float diagonalCoverage(int pixelX, int pixelY, int width, int height, u32 data, float lightThickness) {
        const float positionX = (float)(pixelX) + 0.5f;
        const float positionY = (float)(pixelY) + 0.5f;
        const float normalLength = sqrtf((float)(width)*width + (float)(height)*height);
        const float distance = (float)(height)*positionX - (float)(width)*positionY;
        const float otherDistance = (float)(height)*positionX + (float)(width)*positionY - (float)(width) * (float)(height);
        float nearest = 1e10f;
        if ((data & 0x0800u) != 0) {
            const float value = fabsf(otherDistance) / normalLength;
            nearest = nearest < value ? nearest : value;
        }
        if ((data & 0x1000u) != 0) {
            const float value = fabsf(distance) / normalLength;
            nearest = nearest < value ? nearest : value;
        }
        const float halfWidth = lightThickness * 0.5f;
        return clampUnit(halfWidth + 0.5f - nearest);
    }

    static float arcCoverage(int pixelX, int pixelY, int width, int height, u32 data, float lightThickness) {
        const float centerX = strokeCenter(width, lightThickness);
        const float centerY = strokeCenter(height, lightThickness);
        const bool left = (data & 0x0003u) != 0;
        const bool up = (data & 0x0030u) != 0;
        const float extentX = left ? centerX : (float)(width) - centerX;
        const float extentY = up ? centerY : (float)(height) - centerY;
        const float oppositeExtentX = left ? (float)(width) - centerX : centerX;
        const float oppositeExtentY = up ? (float)(height) - centerY : centerY;
        const float radiusX = extentX < oppositeExtentX ? extentX : oppositeExtentX;
        const float radiusY = extentY < oppositeExtentY ? extentY : oppositeExtentY;
        const float radius = radiusX < radiusY ? radiusX : radiusY;
        const float directionX = (((float)(pixelX) + 0.5f) - centerX) * (left ? -1.0f : 1.0f);
        const float directionY = (((float)(pixelY) + 0.5f) - centerY) * (up ? -1.0f : 1.0f);

        // A circular quarter arc on the short side, with tangent straight
        // tails to the cell edges. An ellipse over the whole half-cell
        // turns into a tall U in ordinary 1:2 terminal cells and meets the
        // neighboring straight glyph with a visible kink.
        float distance = 1e10f;
        if (directionX >= 0.0f && directionY >= 0.0f) {
            distance = fabsf(sqrtf(directionX * directionX + directionY * directionY) - radius);
        }
        const float horizontalX = directionX < radius ? radius : directionX > extentX ? extentX : directionX;
        const float horizontalDistance = sqrtf((directionX - horizontalX) * (directionX - horizontalX) + directionY * directionY);
        distance = distance < horizontalDistance ? distance : horizontalDistance;
        const float verticalY = directionY < radius ? radius : directionY > extentY ? extentY : directionY;
        const float verticalDistance = sqrtf(directionX * directionX + (directionY - verticalY) * (directionY - verticalY));
        distance = distance < verticalDistance ? distance : verticalDistance;
        return clampUnit(lightThickness * 0.5f + 0.5f - distance);
    }

    static float axisAlignedCoverage(u32 data, int pixelX, int pixelY, int width, int height, float lightThickness) {
        const float centerX = strokeCenter(width, lightThickness);
        const float centerY = strokeCenter(height, lightThickness);
        const float heavyThickness = lightThickness * 2.0f;
        const float hLightTop = centerY - lightThickness * 0.5f;
        const float hLightBottom = centerY + lightThickness * 0.5f;
        const float hHeavyTop = centerY - heavyThickness * 0.5f;
        const float hHeavyBottom = centerY + heavyThickness * 0.5f;
        const float hDoubleTop = centerY - lightThickness * 1.5f;
        const float hDoubleBottom = centerY + lightThickness * 1.5f;
        const float vLightLeft = centerX - lightThickness * 0.5f;
        const float vLightRight = centerX + lightThickness * 0.5f;
        const float vHeavyLeft = centerX - heavyThickness * 0.5f;
        const float vHeavyRight = centerX + heavyThickness * 0.5f;
        const float vDoubleLeft = centerX - lightThickness * 1.5f;
        const float vDoubleRight = centerX + lightThickness * 1.5f;

        const u32 left = field(data, 0, 2);
        const u32 right = field(data, 2, 2);
        const u32 up = field(data, 4, 2);
        const u32 down = field(data, 6, 2);

        // Do not split a straight stroke at the cell center: for a stem
        // below one pixel, two antialiased rectangles would otherwise lose
        // coverage where their fractional end caps meet.
        if (up == 0 && down == 0 && left == right && left != 0) {
            if (left == 1) {
                return rectangleCoverage(pixelX, pixelY, 0.0f, hLightTop, (float)(width), hLightBottom);
            }
            if (left == 2) {
                return rectangleCoverage(pixelX, pixelY, 0.0f, hHeavyTop, (float)(width), hHeavyBottom);
            }
            float coverage = rectangleCoverage(pixelX, pixelY, 0.0f, hDoubleTop, (float)(width), hLightTop);
            addRectangle(coverage, pixelX, pixelY, 0.0f, hLightBottom, (float)(width), hDoubleBottom);
            return coverage;
        }
        if (left == 0 && right == 0 && up == down && up != 0) {
            if (up == 1) {
                return rectangleCoverage(pixelX, pixelY, vLightLeft, 0.0f, vLightRight, (float)(height));
            }
            if (up == 2) {
                return rectangleCoverage(pixelX, pixelY, vHeavyLeft, 0.0f, vHeavyRight, (float)(height));
            }
            float coverage = rectangleCoverage(pixelX, pixelY, vDoubleLeft, 0.0f, vLightLeft, (float)(height));
            addRectangle(coverage, pixelX, pixelY, vLightRight, 0.0f, vDoubleRight, (float)(height));
            return coverage;
        }

        const float upBottom = left == 2 || right == 2 ? hHeavyBottom : left != right || down == up ? left == 3 || right == 3 ? hDoubleBottom : hLightBottom : left == 0 && right == 0 ? hLightBottom : hLightTop;
        const float downTop = left == 2 || right == 2 ? hHeavyTop : left != right || up == down ? left == 3 || right == 3 ? hDoubleTop : hLightTop : left == 0 && right == 0 ? hLightTop : hLightBottom;
        const float leftRight = up == 2 || down == 2 ? vHeavyRight : up != down || left == right ? up == 3 || down == 3 ? vDoubleRight : vLightRight : up == 0 && down == 0 ? vLightRight : vLightLeft;
        const float rightLeft = up == 2 || down == 2 ? vHeavyLeft : up != down || right == left ? up == 3 || down == 3 ? vDoubleLeft : vLightLeft : up == 0 && down == 0 ? vLightLeft : vLightRight;

        float coverage = 0.0f;
        if (up == 1) {
            addRectangle(coverage, pixelX, pixelY, vLightLeft, 0.0f, vLightRight, upBottom);
        } else if (up == 2) {
            addRectangle(coverage, pixelX, pixelY, vHeavyLeft, 0.0f, vHeavyRight, upBottom);
        } else if (up == 3) {
            const float leftBottom = left == 3 ? hLightTop : upBottom;
            const float rightBottom = right == 3 ? hLightTop : upBottom;
            addRectangle(coverage, pixelX, pixelY, vDoubleLeft, 0.0f, vLightLeft, leftBottom);
            addRectangle(coverage, pixelX, pixelY, vLightRight, 0.0f, vDoubleRight, rightBottom);
        }
        if (right == 1) {
            addRectangle(coverage, pixelX, pixelY, rightLeft, hLightTop, (float)(width), hLightBottom);
        } else if (right == 2) {
            addRectangle(coverage, pixelX, pixelY, rightLeft, hHeavyTop, (float)(width), hHeavyBottom);
        } else if (right == 3) {
            const float topLeft = up == 3 ? vLightRight : rightLeft;
            const float bottomLeft = down == 3 ? vLightRight : rightLeft;
            addRectangle(coverage, pixelX, pixelY, topLeft, hDoubleTop, (float)(width), hLightTop);
            addRectangle(coverage, pixelX, pixelY, bottomLeft, hLightBottom, (float)(width), hDoubleBottom);
        }
        if (down == 1) {
            addRectangle(coverage, pixelX, pixelY, vLightLeft, downTop, vLightRight, (float)(height));
        } else if (down == 2) {
            addRectangle(coverage, pixelX, pixelY, vHeavyLeft, downTop, vHeavyRight, (float)(height));
        } else if (down == 3) {
            const float leftTop = left == 3 ? hLightBottom : downTop;
            const float rightTop = right == 3 ? hLightBottom : downTop;
            addRectangle(coverage, pixelX, pixelY, vDoubleLeft, leftTop, vLightLeft, (float)(height));
            addRectangle(coverage, pixelX, pixelY, vLightRight, rightTop, vDoubleRight, (float)(height));
        }
        if (left == 1) {
            addRectangle(coverage, pixelX, pixelY, 0.0f, hLightTop, leftRight, hLightBottom);
        } else if (left == 2) {
            addRectangle(coverage, pixelX, pixelY, 0.0f, hHeavyTop, leftRight, hHeavyBottom);
        } else if (left == 3) {
            const float topRight = up == 3 ? vLightLeft : leftRight;
            const float bottomRight = down == 3 ? vLightLeft : leftRight;
            addRectangle(coverage, pixelX, pixelY, 0.0f, hDoubleTop, topRight, hLightTop);
            addRectangle(coverage, pixelX, pixelY, 0.0f, hLightBottom, bottomRight, hDoubleBottom);
        }
        return coverage;
    }

    static float boxDrawingCoverage(u32 codepoint, int pixelX, int pixelY, int width, int height, float lightThickness) {
        const u32 data = boxDrawingData[codepoint - 0x2500u];
        if ((data & 0x1800u) != 0) {
            return diagonalCoverage(pixelX, pixelY, width, height, data, lightThickness);
        }
        if ((data & 0x0400u) != 0) {
            return arcCoverage(pixelX, pixelY, width, height, data, lightThickness);
        }

        const u32 dashCount = field(data, 13, 3);
        const u32 left = field(data, 0, 2);
        const u32 right = field(data, 2, 2);
        const u32 up = field(data, 4, 2);
        const u32 down = field(data, 6, 2);
        if (dashCount != 0) {
            if (left != 0 || right != 0) {
                if (!inDash(pixelX, width, dashCount)) {
                    return 0.0f;
                }
                const float thickness = (left == 2 || right == 2) ? lightThickness * 2.0f : lightThickness;
                const float center = strokeCenter(height, lightThickness);
                return intervalCoverage(pixelY, center - thickness * 0.5f, center + thickness * 0.5f);
            }
            if (!inDash(pixelY, height, dashCount)) {
                return 0.0f;
            }
            const float thickness = (up == 2 || down == 2) ? lightThickness * 2.0f : lightThickness;
            const float center = strokeCenter(width, lightThickness);
            return intervalCoverage(pixelX, center - thickness * 0.5f, center + thickness * 0.5f);
        }
        return axisAlignedCoverage(data, pixelX, pixelY, width, height, lightThickness);
    }

    static float scanLineCoverage(u32 codepoint, int pixelY, int height, float thickness) {
        static const int positions[4] = {1, 3, 7, 9};
        const float center = (float)(height * positions[codepoint - 0x23bau]) / 10.0f;
        return intervalCoverage(pixelY, center - thickness * 0.5f, center + thickness * 0.5f);
    }

    static float dentistryCoverage(u32 codepoint, int pixelX, int pixelY, int width, int height, float thickness) {
        // The straight dentistry brackets hug the cell edges: a full
        // height vertical on the side the symbol names and a full width
        // horizontal at its top or bottom, so consecutive rows chain
        // into one bracket the way the symbols mark a span.
        const bool top = codepoint == 0x23beu || codepoint == 0x23cbu;
        const bool right = codepoint == 0x23cbu || codepoint == 0x23ccu;
        const float vertical = right ? intervalCoverage(pixelX, (float)(width) - thickness, (float)(width)) : intervalCoverage(pixelX, 0.0f, thickness);
        const float horizontal = top ? intervalCoverage(pixelY, 0.0f, thickness) : intervalCoverage(pixelY, (float)(height) - thickness, (float)(height));
        return vertical + horizontal - vertical * horizontal;
    }

    static float insideCoverage(float distance) {
        return 1.0f - smoothStep(-0.5f, 0.5f, distance);
    }

    static float mediaCoverage(u32 codepoint, int pixelX, int pixelY, int width, int height) {
        // The media-control symbols as signed distances inside a square
        // inset into the cell, antialiased over one pixel.
        const float deltaX = (float)(pixelX) + 0.5f - (float)(width) * 0.5f;
        const float deltaY = (float)(pixelY) + 0.5f - (float)(height) * 0.5f;
        const int shortSide = minimum(width, height);
        const float halfSide = (float)(shortSide - 2 * maximum(1, shortSide / 8)) * 0.5f;
        if (codepoint == 0x23f8u) {
            const float barX = fabsf(fabsf(deltaX) - halfSide * 0.55f) - halfSide * 0.35f;
            const float barY = fabsf(deltaY) - halfSide;
            return insideCoverage(barX > barY ? barX : barY);
        }
        if (codepoint == 0x23f9u) {
            const float x = fabsf(deltaX);
            const float y = fabsf(deltaY);
            return insideCoverage((x > y ? x : y) - halfSide * 0.9f);
        }
        if (codepoint == 0x23fau) {
            return insideCoverage(sqrtf(deltaX * deltaX + deltaY * deltaY) - halfSide);
        }
        // The four triangles fold onto one axis with the apex positive.
        const bool vertical = codepoint == 0x23f6u || codepoint == 0x23f7u;
        const bool negative = codepoint == 0x23f4u || codepoint == 0x23f6u;
        const float along = (vertical ? deltaY : deltaX) * (negative ? -1.0f : 1.0f);
        const float across = fabsf(vertical ? deltaX : deltaY);
        const float slope = (across - (halfSide - along) * 0.5f) / 1.118034f;
        const float base = -halfSide - along;
        return insideCoverage(slope > base ? slope : base);
    }

    static float blockCoverage(u32 codepoint, int pixelX, int pixelY, int width, int height) {
        if (codepoint >= 0x2591u && codepoint <= 0x2593u) {
            // The shades as an ordered 4x4 Bayer pattern: pixel-stable at
            // 4, 8, and 12 sixteenths.
            static const int bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
            const int level = (int)(codepoint - 0x2590u) * 4;
            return bayer[(pixelY & 3) * 4 + (pixelX & 3)] < level ? 1.0f : 0.0f;
        }
        int x0 = 0;
        int x1 = 8;
        int y0 = 0;
        int y1 = 8;
        if (codepoint == 0x2580u) {
            y1 = 4;
        } else if (codepoint <= 0x2588u) {
            y0 = 8 - (int)(codepoint - 0x2580u);
        } else if (codepoint <= 0x258fu) {
            x1 = 8 - (int)(codepoint - 0x2588u);
        } else if (codepoint == 0x2590u) {
            x0 = 4;
        } else if (codepoint == 0x2594u) {
            y1 = 1;
        } else if (codepoint == 0x2595u) {
            x0 = 7;
        } else {
            // Quadrant bits: 1 upper left, 2 upper right, 4 lower left,
            // 8 lower right.
            static const u32 quadrants[10] = {0x4, 0x8, 0x1, 0xd, 0x9, 0x7, 0xb, 0x2, 0x6, 0xe};
            const u32 mask = quadrants[codepoint - 0x2596u];
            const int quadrantX = pixelX < width * 4 / 8 ? 0 : 1;
            const int quadrantY = pixelY < height * 4 / 8 ? 0 : 1;
            return (mask & (1u << (u32)(quadrantY * 2 + quadrantX))) != 0 ? 1.0f : 0.0f;
        }
        const bool inside = pixelX >= width * x0 / 8 && pixelX < width * x1 / 8 && pixelY >= height * y0 / 8 && pixelY < height * y1 / 8;
        return inside ? 1.0f : 0.0f;
    }
}

bool synthesizedCodepoint(u32 codepoint) {
    return (codepoint >= 0x2500 && codepoint <= 0x257f) || (codepoint >= 0x2580 && codepoint <= 0x259f) || (codepoint >= 0x23ba && codepoint <= 0x23bf) || codepoint == 0x23cb || codepoint == 0x23cc || (codepoint >= 0x23f4 && codepoint <= 0x23fa);
}

float synthesizedCoverage(u32 codepoint, int x, int y, int width, int height, float lightStroke) {
    lightStroke = lightStroke > 0.0f ? lightStroke : 1.0f;
    if (codepoint >= 0x2500 && codepoint <= 0x257f) {
        return boxDrawingCoverage(codepoint, x, y, width, height, lightStroke);
    }
    if (codepoint >= 0x2580 && codepoint <= 0x259f) {
        return blockCoverage(codepoint, x, y, width, height);
    }
    if (codepoint >= 0x23ba && codepoint <= 0x23bd) {
        return scanLineCoverage(codepoint, y, height, lightStroke);
    }
    if (codepoint == 0x23be || codepoint == 0x23bf || codepoint == 0x23cb || codepoint == 0x23cc) {
        return dentistryCoverage(codepoint, x, y, width, height, lightStroke);
    }
    if (codepoint >= 0x23f4 && codepoint <= 0x23fa) {
        return mediaCoverage(codepoint, x, y, width, height);
    }
    return 0.0f;
}
