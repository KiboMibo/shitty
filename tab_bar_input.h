/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "input_handler.h"

struct Composer;

// Clicking a tab in the band selects it.
//
// It sits ahead of the terminal on the input chain, which is
// first-accepts-wins, so it must claim as little as it can: only a press
// that lands in the band, and only the release that closes such a press.
// A press that began in the grid belongs to the terminal's drag machine
// and its release must reach it even if the pointer has wandered into the
// band since - swallowing that release strands the selection and leaves
// its autoscroll running.
struct TabBarInput: public InputHandler {
    static TabBarInput* create(Composer& composer);
};
