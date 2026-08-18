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
// observed by the caller (T3, from plt::Window::info()). Mirrors
// plt::WindowInfo's x/y/width/height fields directly - this module does
// not resolve, clamp, or otherwise interpret them against a live screen;
// that needs a real NSScreen and stays T3's job when it applies a loaded
// frame. A frame with an off-screen or stale position/size is not this
// module's problem to catch, and loadQuickFrame() accepts one exactly as
// stored.
struct QuickFrame {
    i32 x = 0;
    i32 y = 0;
    u32 width = 0;
    u32 height = 0;
};

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
