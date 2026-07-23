/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "vterm_headless.h"

#include "composer.h"
#include "options.h"
#include "vterm.h"
#include "vterm_host.h"

#include <std/mem/obj_pool.h>

#include <stdexcept>
#include <string>

namespace stl {}

using namespace stl;

namespace {
    struct HeadlessHost final: public VtermHost {
        HeadlessHost(u16 pixelWidth, u16 pixelHeight);

        void osc(int command, const std::string& argument) override;
        bool handlesOsc() const override;
        void bell() override;
        bool handlesPrinter() const override;
        void print(const std::string& output) override;
        void leds(u8 state) override;
        void notify(const std::string& id, const std::string& title, const std::string& body, bool close) override;
        void progress(u32 state, u32 percent) override;
        void windowOperation(u32 operation, u32 first, u32 second) override;
        VtermWindowInfo windowInfo() override;

        u16 pixelWidth;
        u16 pixelHeight;
    };

    struct VtermHeadlessImpl final: public VtermHeadless {
        VtermHeadlessImpl(u16 pixelWidth, u16 pixelHeight);

        void initialize(Composer& composer, u16 pixelWidth, u16 pixelHeight);
        void feed(const u8* data, size_t len) override;

        HeadlessHost host;
        Vterm* vterm = nullptr;
    };
}

HeadlessHost::HeadlessHost(u16 pixelWidth_, u16 pixelHeight_)
    : pixelWidth(pixelWidth_)
    , pixelHeight(pixelHeight_)
{
}

void HeadlessHost::osc(int, const std::string&) {
}

bool HeadlessHost::handlesOsc() const {
    return false;
}

void HeadlessHost::bell() {
}

bool HeadlessHost::handlesPrinter() const {
    return false;
}

void HeadlessHost::print(const std::string&) {
}

void HeadlessHost::leds(u8) {
}

void HeadlessHost::notify(const std::string&, const std::string&, const std::string&, bool) {
}

void HeadlessHost::progress(u32, u32) {
}

void HeadlessHost::windowOperation(u32, u32, u32) {
}

VtermWindowInfo HeadlessHost::windowInfo() {
    VtermWindowInfo info;
    info.pixelWidth = pixelWidth;
    info.pixelHeight = pixelHeight;
    info.screenPixelWidth = pixelWidth;
    info.screenPixelHeight = pixelHeight;
    return info;
}

VtermHeadlessImpl::VtermHeadlessImpl(u16 pixelWidth, u16 pixelHeight)
    : host(pixelWidth, pixelHeight)
{
}

void VtermHeadlessImpl::initialize(Composer& composer, u16 pixelWidth, u16 pixelHeight) {
    vterm = Vterm::create(composer, host, nullptr, 1, 1, pixelWidth, pixelHeight);
}

void VtermHeadlessImpl::feed(const u8* data, size_t len) {
    if (data == nullptr && len != 0) {
        throw std::invalid_argument("headless Vterm input is null");
    }
    if (len == 0) {
        return;
    }
    vterm->feedPty(StringView(data, len));
    while (true) {
        const VtermOutput output = vterm->output();
        if (output.pty.empty() && output.terminal == nullptr) {
            return;
        }
        vterm->consume({output.pty.length(), output.terminal != nullptr});
    }
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
    try {
        VtermHeadlessImpl* result = composer.pool->make<VtermHeadlessImpl>(pixelWidth, pixelHeight);
        result->initialize(composer, pixelWidth, pixelHeight);
        opts.title = title;
        return result;
    } catch (...) {
        opts.title = title;
        throw;
    }
}
