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
#include "vterm_host.h"

#include <std/mem/obj_pool.h>

#include <stdexcept>
#include <string>

using namespace stl;

namespace {
    struct HeadlessHost final: public VtermHost {
        explicit HeadlessHost(Composer& composer);

        void osc(int command, StringView argument) override;
        bool handlesOsc() const override;
        void title(StringView) override;
        void cwd(StringView) override;
        void bell() override;
        bool handlesPrinter() const override;
        void print(StringView output) override;
        void leds(u8 state) override;
        void notify(StringView id, StringView title, StringView body, bool close) override;
        void progress(u32 state, u32 percent) override;
        void windowOperation(u32 operation, u32 first, u32 second) override;
        VtermWindowInfo windowInfo() override;

        Composer& composer;
    };

    struct VtermHeadlessImpl final: public VtermHeadless, public Clipboard {
        explicit VtermHeadlessImpl(Composer& composer);

        void feed(const u8* data, size_t len) override;
        StringView readPrimary() override;
        StringView readClipboard() override;
        void writePrimary(StringView) override;
        void writeClipboard(StringView) override;

        Composer& composer;
        HeadlessHost host;
    };
}

HeadlessHost::HeadlessHost(Composer& composer_)
    : composer(composer_)
{
}

void HeadlessHost::osc(int, StringView) {
}

bool HeadlessHost::handlesOsc() const {
    return false;
}

void HeadlessHost::title(StringView) {
}

void HeadlessHost::cwd(StringView) {
}

void HeadlessHost::bell() {
}

bool HeadlessHost::handlesPrinter() const {
    return false;
}

void HeadlessHost::print(StringView) {
}

void HeadlessHost::leds(u8) {
}

void HeadlessHost::notify(StringView, StringView, StringView, bool) {
}

void HeadlessHost::progress(u32, u32) {
}

void HeadlessHost::windowOperation(u32 operation, u32 first, u32 second) {
    if (operation == 4 && first && second) {
        composer.resize((u16)(second), (u16)(first));
    } else if (operation == 8 && first && second) {
        composer.resize(2 * opts.border + second * composer.glyphWidth, 2 * opts.border + first * composer.glyphHeight);
    }
}

VtermWindowInfo HeadlessHost::windowInfo() {
    VtermWindowInfo info;
    info.screenPixelWidth = composer.pixelWidth;
    info.screenPixelHeight = composer.pixelHeight;
    return info;
}

VtermHeadlessImpl::VtermHeadlessImpl(Composer& composer_)
    : composer(composer_)
    , host(composer)
{
    composer.clipboard = this;
    composer.vterm = Vterm::create(composer, host, nullptr);
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
    const StringView ptyOutput = vterm->ptyOutput();
    if (!ptyOutput.empty()) {
        vterm->consumePtyOutput(ptyOutput.length());
    }
    if (vterm->output() != nullptr) {
        vterm->consume();
    }
}

StringView VtermHeadlessImpl::readPrimary() {
    return {};
}

StringView VtermHeadlessImpl::readClipboard() {
    return {};
}

void VtermHeadlessImpl::writePrimary(StringView) {
}

void VtermHeadlessImpl::writeClipboard(StringView) {
}

VtermHeadless* VtermHeadless::create(Composer& composer) {
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

    composer.setGlyphSize(glyphWidth, glyphHeight);
    composer.resize(pixelWidth, pixelHeight);
    VtermHeadlessImpl* result = composer.pool->make<VtermHeadlessImpl>(composer);
    opts.title = title;
    return result;
}
