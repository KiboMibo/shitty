/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "input_sink.h"

InputSink::~InputSink() noexcept {
    unlink();
}
