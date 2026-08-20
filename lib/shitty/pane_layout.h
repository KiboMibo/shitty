/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "composer.h"
#include "vterm.h"

#include <std/lib/vector.h>

// The pane that fills the window: the composer's grid at origin zero.
// Named once here so no caller has to spell out its own idea of "one
// terminal, whole window" - and so the day panes really divide the
// window, the callers that must stop using it are the ones that still
// name it.
//
// A5-4: this lives with the layout and not in vterm.h. The type
// PaneGeometry belongs in the terminal's header - it is the contract
// both sides include - but a ready-made "pane == window" constructor
// sitting there let everyone who includes vterm.h (which is nearly the
// whole tree) build one without asking layout at all.
PaneGeometry windowPane(const Composer& composer);
