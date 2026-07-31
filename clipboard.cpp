/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "clipboard.h"

#include "composer.h"

#include <plt/window.h>

#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    struct ClipboardImpl final: public Clipboard {
        explicit ClipboardImpl(plt::Window& window);

        bool readAll(bool primary, Buffer& content) override;
        void writePrimary(StringView content) override;
        void writeClipboard(StringView content) override;

        plt::Window& window;
    };
}

ClipboardImpl::ClipboardImpl(plt::Window& window_)
    : window(window_)
{
}

bool ClipboardImpl::readAll(bool primary, Buffer& content) {
    plt::Clipboard* const source = primary ? window.primary() : window.secondary();
    return source->readAll(content);
}

void ClipboardImpl::writePrimary(StringView content) {
    window.primary()->write(content);
}

void ClipboardImpl::writeClipboard(StringView content) {
    window.secondary()->write(content);
}

Clipboard* Clipboard::create(Composer& composer, plt::Window& window) {
    return composer.pool->make<ClipboardImpl>(window);
}
