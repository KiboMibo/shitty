/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "tab_bar_input.h"

#include "composer.h"
#include "options.h"
#include "session.h"

#include <plt/window.h>

#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    struct TabBarInputImpl final: public TabBarInput {
        explicit TabBarInputImpl(Composer& composer_)
            : composer(composer_)
        {
        }

        bool key(const plt::KeyInput&) override {
            return false;
        }

        bool text(const plt::TextInput&) override {
            return false;
        }

        bool pointerMotion(const plt::PointerMotionInput& input) override;
        bool pointerButton(const plt::PointerButtonInput& input) override;

        bool scroll(const plt::ScrollInput&) override {
            return false;
        }

        void focus(bool) override {
            pressed_ = false;
        }

        void pointerPresence(bool) override {
        }

        void flush() override {
        }

        bool insideBand(int pixelY) const;
        size_t sessionAt(int pixelX) const;

        Composer& composer;
        // Whether the button now down was pressed in the band. A press that
        // began in the grid belongs to the terminal's drag machine, and its
        // release must reach the terminal even if the pointer has since
        // wandered up here: swallowing that release strands the selection
        // and latches its autoscroll on forever.
        bool pressed_ = false;
    };
}

bool TabBarInputImpl::insideBand(int pixelY) const {
    if (composer.topInset == 0) {
        return false;
    }
    const int top = composer.opts->border;
    return pixelY >= top && pixelY < top + (int)(composer.topInset);
}

size_t TabBarInputImpl::sessionAt(int pixelX) const {
    const size_t count = composer.sessions->count();
    const int width = (int)(composer.pixelWidth) - 2 * composer.opts->border;
    const int offset = pixelX - composer.opts->border;
    if (width <= 0 || offset < 0 || count == 0) {
        return 0;
    }
    const int segment = width / (int)(count);
    if (segment == 0) {
        return 0;
    }
    const size_t which = (size_t)(offset / segment);
    return which < count ? which : count - 1;
}

bool TabBarInputImpl::pointerMotion(const plt::PointerMotionInput&) {
    // Motion is never claimed. The terminal needs every motion event to
    // extend a selection, including the ones that stray into the band.
    return false;
}

bool TabBarInputImpl::pointerButton(const plt::PointerButtonInput& input) {
    if (composer.sessions == nullptr || composer.sessions->count() < 2) {
        return false;
    }
    if (input.pressed) {
        if (!insideBand(input.pixelY)) {
            return false;
        }
        pressed_ = true;
        const size_t which = sessionAt(input.pixelX);
        if (which != composer.sessions->active()) {
            composer.sessions->activate(which);
            composer.window->requestFrame();
        }
        return true;
    }
    // Only the release that closes a press this handler took. Any other
    // release belongs to the terminal, wherever the pointer happens to be.
    if (!pressed_) {
        return false;
    }
    pressed_ = false;
    return true;
}

TabBarInput* TabBarInput::create(Composer& composer) {
    return composer.pool->make<TabBarInputImpl>(composer);
}
