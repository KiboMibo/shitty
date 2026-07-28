/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

struct Composer;
struct Clipboard;
struct DesktopActions;

struct TestModeInput {
    virtual void testKeyEvent(int key, int scancode, int action, int modifiers) = 0;
    virtual void testTextInput(unsigned codepoint, int modifiers) = 0;
    virtual void testContentScale(float xScale, float yScale) = 0;
    virtual void testClipboard(Clipboard* clipboard) = 0;
    virtual void testDesktopActions(DesktopActions* actions) = 0;
};

int runTestMode(Composer& composer, TestModeInput& input, int controlFd, int argc, char* argv[]);
