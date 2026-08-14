/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/sys/types.h>

// The coverage the renderers synthesize instead of consulting a font:
// box drawing, DEC scan lines, the block elements, the straight
// dentistry brackets, and the media-control symbols - the last two
// because most monospace fonts simply lack them. Fonts rasterize
// these with fractional ink that leaves background seams between cells;
// the synthesized geometry lands on exact cell pixels, so adjacent
// cells meet with no gap. This is the CPU mirror of the same functions
// in render.comp - the two must stay in lockstep.
bool synthesizedCodepoint(u32 codepoint);
float synthesizedCoverage(u32 codepoint, int x, int y, int width, int height, float lightStroke);
