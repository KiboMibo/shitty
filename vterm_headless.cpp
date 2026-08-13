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
#include <plt/platform.h>
#include <plt/platform_headless.h>

#include <std/dbg/insist.h>
#include <std/ios/input.h>
#include <std/ios/out.h>
#include <std/ios/output.h>
#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    // The headless pty face: one reusable full-length chunk, and a send
    // forwards to whatever Output the host installed. No child, no
    // drain thread; the read side never delivers.
    struct OutputPtyHandle final: public PtyHandle {
        explicit OutputPtyHandle(Composer& composer);

        void resize(const PtySize& size) override;
        void engage() override;
        Chunk* allocate(size_t len) override;
        void send(Chunk* chunk, size_t len) override;
        Chunk* acquire() override;
        void release(Chunk* chunks) override;

        struct HeadlessChunk final: public Chunk {
            void* data() override;
            size_t length() override;
            Chunk* next() override;

            Buffer payload_;
            size_t used_ = 0;
            bool loaned_ = false;
        };

        Composer& composer;
        HeadlessChunk chunk_;
    };

    struct VtermHeadlessImpl final: public VtermHeadless {
        explicit VtermHeadlessImpl(Composer& composer);

        void feed(const u8* data, size_t len) override;
        Vterm* terminal() override;

        Composer& composer;
        Vterm* terminal_ = nullptr;
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

void* OutputPtyHandle::HeadlessChunk::data() {
    return payload_.mutData();
}

size_t OutputPtyHandle::HeadlessChunk::length() {
    return used_;
}

PtyHandle::Chunk* OutputPtyHandle::HeadlessChunk::next() {
    return nullptr;
}

OutputPtyHandle::OutputPtyHandle(Composer& composer_)
    : composer(composer_)
{
}

void OutputPtyHandle::resize(const PtySize&) {
}

void OutputPtyHandle::engage() {
}

PtyHandle::Chunk* OutputPtyHandle::allocate(size_t len) {
    STD_INSIST(!chunk_.loaned_);
    chunk_.payload_.reset();
    chunk_.payload_.grow(len);
    chunk_.payload_.seekAbsolute(len);
    chunk_.used_ = len;
    chunk_.loaned_ = true;
    return &chunk_;
}

void OutputPtyHandle::send(Chunk* chunk, size_t len) {
    STD_INSIST(chunk == &chunk_ && chunk_.loaned_);
    chunk_.loaned_ = false;
    composer.ptyOutput->write(chunk_.payload_.data(), len);
    composer.ptyOutput->flush();
}

PtyHandle::Chunk* OutputPtyHandle::acquire() {
    composer.platform->scheduler()->current()->park();
    return nullptr;
}

void OutputPtyHandle::release(Chunk*) {
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
    terminal_->feedPty(StringView(data, len));
    if (terminal_->output() != nullptr) {
        terminal_->consume();
    }
}

Vterm* VtermHeadlessImpl::terminal() {
    return terminal_;
}

PtyHandle* createHeadlessPtyHandle(Composer& composer) {
    return composer.pool->make<OutputPtyHandle>(composer);
}

VtermHeadless* VtermHeadless::create(Composer& composer, VtermTraceFactory* traceFactory) {
    constexpr u16 columns = 80;
    constexpr u16 rows = 24;
    constexpr u16 glyphWidth = 1;
    constexpr u16 glyphHeight = 1;
    const u16 pixelWidth = 2 * composer.borderPixels() + columns * glyphWidth;
    const u16 pixelHeight = 2 * composer.borderPixels() + rows * glyphHeight;

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
    Vterm* const vterm = Vterm::create(*composer.pool, composer, *createHeadlessPtyHandle(composer), traceFactory);
    result->terminal_ = vterm;
    composer.resizedListeners.pushBack(composer.pool->make<CallHeadlessResize>(vterm));
    composer.fontChangedListeners.pushBack(composer.pool->make<CallHeadlessFontChanged>(vterm));
    return result;
}
