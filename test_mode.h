/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

struct Composer;
struct TestInput;

int runTestMode(Composer& composer, TestInput& input, int controlFd, int argc, char* argv[]);
