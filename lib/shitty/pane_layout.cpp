/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "pane_layout.h"

using namespace stl;

// A8: one terminal, the whole window. The origin is zero and the grid is
// the composer's: the only place outside Composer itself that is allowed
// to read the window's grid and call the answer a pane.
PaneGeometry windowPane(const Composer& composer) {
    return {.columns = composer.columns, .rows = composer.rows};
}
