/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

struct Composer;

struct Application {
    virtual int run(int argc, char* argv[]) = 0;

    static Application* create(Composer& composer);
};
