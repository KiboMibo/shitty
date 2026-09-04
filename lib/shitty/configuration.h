/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

struct Composer;

// Owns immutable Options snapshots and delivers SIGUSR1 reloads on the
// platform loop. initialize() consumes configuration arguments from argv in
// the same way as the startup parser; start() requires composer.vt.platform.
struct Config {
    virtual void initialize(int* argc, char* argv[]) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void reload() = 0;

    static Config* create(Composer& composer);
};
