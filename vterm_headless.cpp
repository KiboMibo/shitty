/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "vterm_headless.h"

#include "fatal.h"

#include "composer.h"
#include "listener.h"
#include "options.h"
#include "pty.h"
#include "vterm.h"

#include <plt/fiber.h>
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
    struct HeadlessPty final: Pty {
        explicit HeadlessPty(Composer& composer_)
            : composer(composer_)
        {
            mutex_ = composer.platform->scheduler()->createMutex(*composer.pool);
        }

        Output* output() override {
            return composer.ptyOutput;
        }

        plt::FiberMutex& mutex() override {
            return *mutex_;
        }

        void stop() override {
        }

        void bindTerminal(Vterm*) override {
            // The headless host feeds its terminal directly.
        }

        bool drained() const override {
            return true;
        }

        size_t tryWrite(const u8* data, size_t len) override {
            // The capture sink has no kernel buffer behind it: every byte
            // is accepted on the spot.
            composer.ptyOutput->write(data, len);
            composer.ptyOutput->flush();
            return len;
        }

        Composer& composer;
        plt::FiberMutex* mutex_ = nullptr;
    };

    struct VtermHeadlessImpl final: public VtermHeadless {
        explicit VtermHeadlessImpl(Composer& composer);

        void feed(const u8* data, size_t len) override;

        Composer& composer;
    };

    // The headless host owns its terminal for the process lifetime, so it
    // also owns the resize and font deliveries a session set would make.
    struct CallHeadlessResize final: public Listener {
        explicit CallHeadlessResize(Vterm* terminal);

        void onListen(void*) override;

        Vterm* terminal;
    };

    struct CallHeadlessFontChanged final: public Listener {
        explicit CallHeadlessFontChanged(Vterm* terminal);

        void onListen(void*) override;

        Vterm* terminal;
    };
}

VtermHeadlessImpl::VtermHeadlessImpl(Composer& composer_)
    : composer(composer_)
{
}

CallHeadlessResize::CallHeadlessResize(Vterm* terminal_)
    : terminal(terminal_)
{
}

void CallHeadlessResize::onListen(void*) {
    terminal->windowResized();
}

CallHeadlessFontChanged::CallHeadlessFontChanged(Vterm* terminal_)
    : terminal(terminal_)
{
}

void CallHeadlessFontChanged::onListen(void*) {
    terminal->fontChanged();
}

void VtermHeadlessImpl::feed(const u8* data, size_t len) {
    if (data == nullptr && len != 0) {
        raiseError(StringView(u8"headless Vterm input is null"));
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
    const u16 pixelWidth = 2 * composer.opts->border + columns * glyphWidth;
    const u16 pixelHeight = 2 * composer.opts->border + rows * glyphHeight;

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
    composer.pty = composer.pool->make<HeadlessPty>(composer);
    Vterm* const vterm = Vterm::create(*composer.pool, composer, traceFactory);
    composer.resizedListeners.pushBack(composer.pool->make<CallHeadlessResize>(vterm));
    composer.fontChangedListeners.pushBack(composer.pool->make<CallHeadlessFontChanged>(vterm));
    return result;
}
