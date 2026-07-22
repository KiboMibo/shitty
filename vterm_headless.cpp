#include "vterm_headless.h"

#include "composer.h"
#include "options.h"
#include "pty.h"
#include "vterm.h"
#include "vterm_host.h"

#include <std/mem/obj_pool.h>

#include <cerrno>
#include <stdexcept>
#include <string>


namespace stl {}
using namespace stl;

namespace {
    class HeadlessPty final: public Pty {
    public:
        int fd() const override;
        ssize_t read(u8* buffer, size_t size) override;
        ssize_t write(const u8* buffer, size_t size) override;
        void resize(u16 columns, u16 rows) override;
    };

    class HeadlessHost final: public VtermHost {
    public:
        HeadlessHost(u16 pixelWidth, u16 pixelHeight);

        bool present(const Frame& frame) override;
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

    private:
        u16 pixelWidth;
        u16 pixelHeight;
    };

    class VtermHeadlessImpl final: public VtermHeadless {
    public:
        VtermHeadlessImpl(u16 pixelWidth, u16 pixelHeight);

        void initialize(Composer& composer, u16 pixelWidth, u16 pixelHeight);
        void feed(const u8* data, size_t len) override;

    private:
        HeadlessPty pty;
        HeadlessHost host;
        Vterm* vterm = nullptr;
    };
}

int HeadlessPty::fd() const {
    return -1;
}

ssize_t HeadlessPty::read(u8*, size_t) {
    errno = EAGAIN;
    return -1;
}

ssize_t HeadlessPty::write(const u8*, size_t size) {
    return size;
}

void HeadlessPty::resize(u16, u16) {
}

HeadlessHost::HeadlessHost(u16 pixelWidth_, u16 pixelHeight_)
    : pixelWidth(pixelWidth_)
    , pixelHeight(pixelHeight_)
{
}

bool HeadlessHost::present(const Frame&) {
    return true;
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
    vterm = Vterm::create(composer, host, pty, 1, 1, pixelWidth, pixelHeight);
}

void VtermHeadlessImpl::feed(const u8* data, size_t len) {
    if (data == nullptr && len != 0) {
        throw std::invalid_argument("headless Vterm input is null");
    }
    if (len == 0) {
        return;
    }
    vterm->feedPtyOutput(data, len);
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
