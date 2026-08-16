/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

namespace stl {
    class ObjPool;
}

struct Composer;

// macOS-only chrome: registers a Carbon global hotkey (RegisterEventHotKey,
// parsed from Options::quickHotkey) that calls toggleQuickWindow() on every
// press, from anywhere - including over another application's fullscreen
// space. Creates a self-contained pool object, by the same shape as
// createCsdTabsUi() in ui_csd_tabs.h. Only wired up when
// Options::quick is true; off macOS nothing defines this.
void createQuickHotkey(stl::ObjPool& owner, Composer& composer);

// Shows the quick-terminal window if it is hidden, hides it if it is
// shown. The one entry point createQuickHotkey()'s hotkey handler calls;
// declared here so the hotkey module can call it without depending on
// application.cpp's other internals, defined there instead since it is
// the application, not the hotkey module, that owns composer.window's
// lifecycle. The window-level behavior behind it - placement, level,
// collection behavior, hide-on-resign-key - lives in
// ext/plt/platform_cocoa.mm.
void toggleQuickWindow(Composer& composer);
