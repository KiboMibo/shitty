/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

struct Composer;

struct TestInput {
    virtual void key(int key, int scancode, int action, int modifiers) = 0;
    // A key event with distinct layout and base-layout codepoints, the way
    // a non-Latin layout delivers them.
    virtual void layoutKey(int key, int action, int modifiers, unsigned layoutCodepoint, unsigned shiftedCodepoint, unsigned baseCodepoint) = 0;
    virtual void text(unsigned codepoint, int modifiers) = 0;
    virtual void contentScale(float xScale, float yScale) = 0;

    static TestInput* create(Composer& composer);
};
