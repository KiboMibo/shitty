/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

namespace plt {
    struct InputSink;
}

struct Composer;

plt::InputSink* createInputRouter(Composer& composer);
