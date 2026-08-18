/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "quick_frame_store.h"

namespace stl {
    class ObjPool;
}

struct Composer;

// macOS-only chrome: registers up to two Carbon global hotkeys
// (RegisterEventHotKey) sharing one event handler, from anywhere -
// including over another application's fullscreen space:
//   - quickHotkey (Options, always non-empty) calls toggleQuickWindow()
//     on every press.
//   - quickFullscreenHotkey (Options, empty means disabled) toggles the
//     window between its current frame and the full screen, entirely
//     inside ui_quick_hotkey.mm via Window::renderContext() - the same
//     escape hatch to the concrete NSWindow that ui_csd_tabs.mm uses -
//     rather than the abstract Window interface, since native
//     -toggleFullScreen: is a no-op here (the quick window's
//     collectionBehavior deliberately omits FullScreenPrimary).
// quickRememberFrame (Options) additionally observes the window
// resigning key status - the one event both the explicit hotkey hide
// above and hide-on-resign-key (WindowImpl::focused, platform_cocoa.mm)
// share - to persist its manually dragged or resized frame through T2's
// quick_frame_store, again via renderContext() rather than a new
// Window/WindowEvents contract point.
//
// Creates a self-contained pool object, by the same shape as
// createCsdTabsUi() in ui_csd_tabs.h. Only wired up when Options::quick
// is true; off macOS nothing defines this.
//
// Returns whether quickHotkey itself ended up registered; unrelated to
// quickFullscreenHotkey, which fails on its own with its own diagnostic
// and never affects this return value or the show/hide hotkey. An
// unparsable chord, a chord with no modifier, or a failed Carbon
// registration all print a diagnostic and return false rather than
// raising - the caller needs to know when the quick-terminal window
// would otherwise become permanently unreachable and fall back to
// showing it normally.
bool createQuickHotkey(stl::ObjPool& owner, Composer& composer);

// Shows the quick-terminal window if it is hidden, hides it if it is
// shown. The one entry point createQuickHotkey()'s hotkey handler calls;
// declared here so the hotkey module can call it without depending on
// application.cpp's other internals, defined there instead since it is
// the application, not the hotkey module, that owns composer.window's
// lifecycle. The window-level behavior behind it - placement, level,
// collection behavior, hide-on-resign-key - lives in
// ext/plt/platform_cocoa.mm.
void toggleQuickWindow(Composer& composer);

// Applies `frame` (T2's QuickFrame - position and content size, both in
// points) to the quick window's concrete NSWindow in one atomic
// -setFrame:, adding this window's own titlebar height back onto the
// saved content size and clamping the resulting whole frame - titlebar
// included - into the screen the frame was saved on.
//
// "Saved on", not "currently on": by the time this runs, requestShowAt()
// has already put the window on the screen under the pointer
// (WindowImpl::topOfActiveScreenFrame, platform_cocoa.mm), so the
// window's own screen answers about the pointer rather than about the
// frame. The screen is found among NSScreen.screens by the saved origin
// instead - restoring against the pointer's screen was what let a frame
// cross displays and be clamped into a screen it had never been on
// (R2-qa round 2, B4).
//
// The one remaining approximate case is a frame saved on a display that
// is no longer attached: its origin matches no screen, the window's
// current screen is used, and the frame may not fit it. That result is
// deliberately never persisted back over the saved frame, so an
// approximation stays one show long instead of replacing the user's own
// placement permanently.
//
// Declared here rather than application.cpp because it reaches the
// concrete NSWindow through Window::renderContext() - see
// ui_quick_hotkey.mm and, for the same pattern doing considerably more
// with it, ui_csd_tabs.mm. False - the window untouched - when there is
// no concrete NSWindow to reach at all: a non-Cocoa backend, or no
// native handle yet.
bool applyQuickFrameToWindow(Composer& composer, const QuickFrame& frame);
