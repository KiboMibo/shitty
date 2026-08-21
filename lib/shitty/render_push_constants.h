/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/sys/types.h>

// The block render.comp reads, in its declaration order. One definition
// for both GPU backends, because they compile the same shader source -
// spirv-cross hands Metal what Vulkan gets - and so they cannot disagree
// about this block without one of them reading garbage.
//
// R9-1 is why it lives here rather than once per backend. It used to be
// declared twice, with a sizeof() assertion each, and the two were meant
// to guard exactly this. Adding four fields updated one assertion and
// left the other naming the old size: the guard was itself the thing
// that drifted, and it drifted silently because only one of the two
// backends compiles on the machine the change was written on. One
// definition and one assertion cannot do that.
struct GpuPushConstants {
    u32 glyphWidth;
    u32 glyphHeight;
    float boxDrawingStroke;
    u32 columns;
    u32 rows;
    // The far edge of the pane being drawn, not of the surface: the
    // first pixel column and row this dispatch may not touch.
    u32 outputWidth;
    u32 outputHeight;
    // Where cell 0,0 starts on the output - the pane's rectangle plus
    // the content insets.
    u32 originX;
    u32 originY;
    u32 cursorColor;
    i32 cursorX;
    i32 cursorY;
    u32 cursorStyle;
    u32 screenReverseVideo;
    i32 selectionLeft;
    i32 selectionTop;
    i32 selectionRight;
    i32 selectionBottom;
    u32 rectangularSelection;
    u32 showWraps;
    u32 selectionForeground;
    u32 selectionBackground;
    u32 selectionColorMask;
    u32 blinkVisible;
    u32 cursorBlink;
    u32 hoveredHyperlink;
    u32 hoveredLinkBegin;
    u32 hoveredLinkEnd;
    u32 updateCount;
    // F9: the pane's near edge. outputWidth/outputHeight above are its
    // far edge and originX/originY are where its grid starts inside it;
    // what was missing was where the pane itself starts, without which
    // the padding between the two cannot be named.
    u32 paneLeft;
    u32 paneTop;
    // The colour the fill pass paints, packed in the low 24 bits - and
    // the fill-pass flag in bit 24.
    //
    // R9-2 is why the flag lives here instead of in a field of its own.
    // Vulkan guarantees maxPushConstantsSize of only 128 bytes, and
    // devices are entitled to offer exactly that; a field of its own put
    // this block at 132 and over the floor. The flag is one bit and a
    // packed colour has a whole spare byte above it, so it costs nothing
    // to carry it there - and unpackColor() reads three 8-bit fields, so
    // the bit cannot leak into any channel.
    u32 paneBackgroundAndFill;
};

// Exactly the floor Vulkan guarantees. Not a coincidence and not slack
// to spend: a field added here without one removed puts the block over
// the smallest maxPushConstantsSize the specification allows, on devices
// that are within their rights to offer no more.
// The fill-pass flag, in the byte a packed 24-bit colour leaves spare.
constexpr u32 fillPassBit = 1u << 24;

static_assert(sizeof(GpuPushConstants) == 128, "GPU push constant block must fit the 128-byte floor Vulkan guarantees");
