/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/str/view.h>
#include <std/sys/types.h>

namespace stl {
    class StringBuilder;
}

// A6: the quick-terminal window's manually set position and size, as last
// observed by the caller (T3), entirely in points - x/y the window
// frame's origin, width/height the content view's size.
//
// Points rather than plt::WindowInfo's own mix of points and backing
// pixels on purpose: a backing pixel means a different amount of screen
// on every display, so a frame saved on a 2x laptop panel came back
// doubled on a 1x external monitor, was persisted that way on the next
// hide, and halved again on the way back - the user's placement
// destroyed one show at a time (R2-qa round 2, B4). A point means the
// same thing everywhere, and the arithmetic disappears with the units.
//
// This module still does not resolve or clamp a frame against a live
// screen; quickFrameTarget() below is where that happens, and it takes
// the screen from its caller. A frame with an off-screen or stale
// position/size is not this module's problem to catch, and
// loadQuickFrame() accepts one exactly as stored.
struct QuickFrame {
    i32 x = 0;
    i32 y = 0;
    u32 width = 0;
    u32 height = 0;
};

// A rectangle in points: a screen's usable area (Cocoa's
// NSScreen.visibleFrame - a secondary display's origin is nowhere near
// zero, which is the whole point of carrying it) or the window frame
// resolved against one.
struct QuickFrameRect {
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
};

struct QuickFrameTarget {
    // The window frame to apply, titlebar included.
    QuickFrameRect frame;
    // True when the frame above is not the saved one: it had to be
    // shrunk or moved to fit. The caller must not persist a clamped
    // frame back over the saved one - the screen it was clamped into is
    // a guess whenever the display it was saved on is gone, and writing
    // the guess back replaces the user's own placement with it for good
    // (R2-qa round 2, B4).
    bool clamped = false;
};

// Resolves a saved frame into the window frame to apply, in points:
// `visible` is the usable area of the screen it is being restored on,
// `titlebarHeight` the chrome above the content (0 where there is none,
// e.g. the portable fallback which has no window chrome to ask about).
//
// The whole frame - titlebar included - is what gets clamped into
// `visible`, not the content alone: clamping the content and only then
// adding the titlebar let a window at the bottom edge stick its titlebar
// out above the visible area (R2-qa round 2, Z2).
//
// Pure and screen-agnostic on purpose. This is the one implementation of
// the clamp, shared by the Cocoa path (ui_quick_hotkey.mm, which
// resolves `visible` from the NSScreen the frame was saved on) and the
// portable fallback (application.cpp): the previous two independent
// implementations left the regression coverage standing on the one that
// does not run on macOS (R2-qa round 2, I8).
QuickFrameTarget quickFrameTarget(const QuickFrame& frame, const QuickFrameRect& visible, double titlebarHeight);

// Builds the default frame store path from the main config file path
// (Options::configPath): alongside it, same directory, with its
// extension (the last '.' in the file name, if any) replaced by
// "-quick-frame" - e.g. ~/.config/shitty/shitty.toml becomes
// ~/.config/shitty/shitty-quick-frame. Deriving it from configPath
// instead of re-walking XDG_CONFIG_HOME/HOME keeps this module free of
// any Brand dependency, and guarantees agreement with whichever config
// file this process actually resolved, -config override included.
//
// False - out untouched - when configPath is empty: OptionsParser
// leaves it that way only when no HOME and no XDG_CONFIG_HOME are set,
// and there is nowhere sane to put a state file either, same as it
// already means no persistence for the main config.
bool defaultQuickFramePath(stl::StringView configPath, stl::StringBuilder& out);

// Reads the saved frame from `path`. False - out untouched - when the
// file is missing, unreadable, or does not parse completely (any of the
// four fields absent or out of shape): a corrupt or absent state file is
// not an error, the caller treats it exactly like "no saved frame yet"
// and falls back to quickGeometry (A6).
//
// The keys carry their unit ("x-points" and friends), which is also how
// a file written by the previous generation - "x"/"width", sizes in
// backing pixels - is retired: none of its keys are recognized, so it
// parses incompletely and is treated as absent, exactly as above. There
// is deliberately no migration; the units a pixel-sized frame was
// written in are not recoverable without the display it came from
// (R2-qa round 2, B4).
bool loadQuickFrame(stl::StringView path, QuickFrame& out);

// Writes `frame` to `path` atomically: a temporary file in the same
// directory (named after this process's pid, so two live processes
// racing on the same path - always true under quickCompanion - never
// collide with each other), then rename() over the target. A reader
// therefore never observes a half-written file, and a failure at any
// point before the rename leaves the previous frame (or nothing) at
// `path` exactly as it was.
//
// Returns false when the write could not be completed at all - the
// parent directory is missing, unwritable, or some other I/O error -
// and cleans up its own temporary file first. Persisting a frame is
// best-effort: the caller logs and moves on rather than failing startup
// over it.
bool saveQuickFrame(stl::StringView path, const QuickFrame& frame);
