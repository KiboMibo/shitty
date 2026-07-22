#pragma once

struct Composer;
struct Vterm;

struct TestModeInput {
    virtual void attachTestVterm(Vterm& terminal) = 0;
    virtual void testKeyEvent(int key, int scancode, int action, int modifiers) = 0;
    virtual void testTextInput(unsigned codepoint, int modifiers) = 0;
};

int runTestMode(Composer& composer, TestModeInput& input, int controlFd, int argc, char* argv[]);
