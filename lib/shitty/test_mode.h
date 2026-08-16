/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

struct Composer;
struct TestInput;

namespace plt {
    struct FrameCallback;
    struct WindowEvents;
}

int runTestMode(Composer& composer, TestInput& input, plt::WindowEvents& events, plt::FrameCallback& frame, int controlFd, int argc, char* argv[]);
