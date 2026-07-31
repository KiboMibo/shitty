/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "desktop_actions.h"

#include "composer.h"

#include <plt/window.h>

#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>

#include <spawn.h>

using namespace stl;

extern char** environ;

namespace {
    struct DesktopActionsImpl final: public DesktopActions {
        explicit DesktopActionsImpl(plt::Window& window);

        void openUri(StringView uri) override;
        void pointerIcon(PointerIcon icon) override;

        plt::Window& window;
        Buffer uriBuffer;
    };
}

DesktopActionsImpl::DesktopActionsImpl(plt::Window& window_)
    : window(window_)
{
}

void DesktopActionsImpl::openUri(StringView uri) {
    uriBuffer.reset();
    uriBuffer.append(uri.data(), uri.length());
    char* const arguments[] = {
        (char*)("xdg-open"),
        uriBuffer.cStr(),
        nullptr,
    };
    pid_t pid = -1;
    posix_spawnp(&pid, arguments[0], nullptr, nullptr, arguments, environ);
}

void DesktopActionsImpl::pointerIcon(PointerIcon icon) {
    window.requestPointerIcon(icon == PointerIcon::Link ? plt::PointerIcon::Pointer : plt::PointerIcon::Text);
}

DesktopActions* DesktopActions::create(Composer& composer, plt::Window& window) {
    return composer.pool->make<DesktopActionsImpl>(window);
}
