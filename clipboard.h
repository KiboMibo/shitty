/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

namespace stl {
    class Buffer;
    class StringView;
}

namespace plt {
    struct Window;
}

struct Composer;

struct Clipboard {
    // Fiber-only: appends the whole selection to content while the event
    // loop keeps running. False on failure or timeout.
    virtual bool readAll(bool primary, stl::Buffer& content) = 0;
    virtual void writePrimary(stl::StringView content) = 0;
    virtual void writeClipboard(stl::StringView content) = 0;

    static Clipboard* create(Composer& composer, plt::Window& window);
};
