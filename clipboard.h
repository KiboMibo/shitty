/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

namespace stl {
    class Output;
    class StringView;
}

struct Clipboard {
    // The clipboard owns output and deletes it after the asynchronous read.
    virtual void readPrimary(stl::Output* output) = 0;
    virtual void readClipboard(stl::Output* output) = 0;
    virtual void writePrimary(stl::StringView content) = 0;
    virtual void writeClipboard(stl::StringView content) = 0;
};
