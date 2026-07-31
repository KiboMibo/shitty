/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "vterm_headless.h"

#include "clipboard.h"
#include "composer.h"
#include "options.h"
#include "vterm.h"

#include <plt/platform_headless.h>

#include <std/ios/out.h>
#include <std/ios/output.h>
#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>

#include <stdexcept>
#include <string>

using namespace stl;

namespace {
    struct VtermHeadlessImpl final: public VtermHeadless, public Clipboard {
        explicit VtermHeadlessImpl(Composer& composer);

        void feed(const u8* data, size_t len) override;
        bool readAll(bool primary, Buffer& content) override;
        void writePrimary(StringView) override;
        void writeClipboard(StringView) override;

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

bool VtermHeadlessImpl::readAll(bool, Buffer&) {
    return true;
}

void VtermHeadlessImpl::writePrimary(StringView) {
}

void VtermHeadlessImpl::writeClipboard(StringView) {
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
    composer.clipboard = result;
    if (composer.ptyOutput == nullptr) {
        composer.ptyOutput = createNullOutput(composer.pool);
    }
    composer.vterm = Vterm::create(composer, traceFactory);
    opts.title = title;
    return result;
}
