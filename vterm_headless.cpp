/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "vterm_headless.h"

#include "composer.h"
#include "options.h"
#include "pty.h"
#include "vterm.h"

#include <plt/mutex.h>
#include <plt/platform_headless.h>

#include <std/ios/input.h>
#include <std/ios/out.h>
#include <std/ios/output.h>
#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>

#include <stdexcept>
#include <string>

using namespace stl;

namespace {
    struct HeadlessPtyInput final: Input {
        size_t readImpl(void*, size_t) override {
            return 0;
        }
    };

    struct HeadlessPty final: Pty {
        explicit HeadlessPty(Composer& composer_)
            : composer(composer_)
        {
        }

        Input* input() override {
            return &input_;
        }

        Output* output() override {
            return composer.ptyOutput;
        }

        Composer& composer;
        HeadlessPtyInput input_;
    };

    struct VtermHeadlessImpl final: public VtermHeadless {
        explicit VtermHeadlessImpl(Composer& composer);

        void feed(const u8* data, size_t len) override;

        Composer& composer;
    };
}

VtermHeadlessImpl::VtermHeadlessImpl(Composer& composer_)
    : composer(composer_)
{
}

void VtermHeadlessImpl::feed(const u8* data, size_t len) {
    if (data == nullptr && len != 0) {
        throw std::invalid_argument("headless Vterm input is null");
    }
    if (len == 0) {
        return;
    }
    Vterm* const vterm = composer.vterm;
    vterm->feedPty(StringView(data, len));
    if (vterm->output() != nullptr) {
        vterm->consume();
    }
}

VtermHeadless* VtermHeadless::create(Composer& composer, VtermTraceFactory* traceFactory) {
    constexpr u16 columns = 80;
    constexpr u16 rows = 24;
    constexpr u16 glyphWidth = 1;
    constexpr u16 glyphHeight = 1;
    const u16 pixelWidth = 2 * opts.border + columns * glyphWidth;
    const u16 pixelHeight = 2 * opts.border + rows * glyphHeight;

    const char* title = opts.title;
    if (opts.title == nullptr) {
        opts.title = "";
    }

    composer.platform = plt::createHeadlessPlatform(*composer.pool);
    composer.window = composer.platform->createWindow(
        *composer.pool,
        {
            .width = pixelWidth,
            .height = pixelHeight,
        }
    );
    composer.setGlyphSize(glyphWidth, glyphHeight);
    composer.resize(pixelWidth, pixelHeight);
    VtermHeadlessImpl* result = composer.pool->make<VtermHeadlessImpl>(composer);
    if (composer.ptyOutput == nullptr) {
        composer.ptyOutput = createNullOutput(composer.pool);
    }
    composer.ptyMutex = composer.pool->make<plt::FiberMutex>();
    composer.pty = composer.pool->make<HeadlessPty>(composer);
    Vterm::create(composer, traceFactory);
    opts.title = title;
    return result;
}
