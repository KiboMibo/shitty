/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

namespace plt {
    struct KeyInput;
}

struct Composer;

// Rewrites key chords according to the remap option before anything else
// consumes them: the router applies it once per event, so both the
// application shortcuts and the terminal encoders see the rewritten chord.
// apply returns false when the chord maps to none and must be dropped.
struct InputRemap {
    virtual bool apply(plt::KeyInput& input) = 0;

    static InputRemap* create(Composer& composer);
};
