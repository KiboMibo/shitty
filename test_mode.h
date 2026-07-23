/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#ifndef SHITTY_FOR_TESTS
    #error "test_mode.h is available only in the SHITTY_FOR_TESTS build"
#endif

struct Composer;
struct Vterm;

struct TestModeInput {
    virtual void attachTestVterm(Vterm& terminal) = 0;
    virtual void testKeyEvent(int key, int scancode, int action, int modifiers) = 0;
    virtual void testTextInput(unsigned codepoint, int modifiers) = 0;
};

int runTestMode(Composer& composer, TestModeInput& input, int controlFd, int argc, char* argv[]);
