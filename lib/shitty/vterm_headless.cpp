/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "vterm_headless.h"

#include "pty.h"
#include "vterm.h"

#include <lib/vterm/fatal.h>
#include <lib/vterm/listener.h>
#include <lib/vterm/vt_state.h>

#include <std/ios/out.h>
#include <std/ios/input.h>
#include <std/dbg/insist.h>
#include <std/ios/output.h>
#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>

#include <plt/fiber.h>
#include <plt/platform.h>
#include <plt/platform_headless.h>

using namespace stl;

namespace {
    // The headless pty face: one reusable full-length chunk, and a send
    // forwards to whatever Output the host installed. No child, no
    // drain thread; the read side never delivers.
    struct OutputPtyHandle final: public PtyHandle {
        OutputPtyHandle(VtState& state, Output& sink);

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

        VtState& state;
        Output& sink;
        HeadlessChunk chunk_;
    };

    struct VtermHeadlessImpl final: public VtermHeadless {
        explicit VtermHeadlessImpl(VtState& state);

        void feed(const u8* data, size_t len) override;
        Vterm* terminal() override;

        VtState& state;
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

OutputPtyHandle::OutputPtyHandle(VtState& state_, Output& sink_)
    : state(state_)
    , sink(sink_)
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
    sink.write(chunk_.payload_.data(), len);
    sink.flush();
}

PtyHandle::Chunk* OutputPtyHandle::acquire() {
    state.platform->scheduler()->current()->park();
    return nullptr;
}

void OutputPtyHandle::release(Chunk*) {
}

VtermHeadlessImpl::VtermHeadlessImpl(VtState& state_)
    : state(state_)
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

VtermHeadless* VtermHeadless::create(VtState& state, VtermTraceFactory* traceFactory, Output* ptyCapture) {
    constexpr u16 columns = 80;
    constexpr u16 rows = 24;
    constexpr u16 glyphWidth = 1;
    constexpr u16 glyphHeight = 1;
    const u16 pixelWidth = 2 * state.borderPixels() + columns * glyphWidth;
    const u16 pixelHeight = 2 * state.borderPixels() + rows * glyphHeight;

    state.platform = plt::createHeadlessPlatform(*state.pool);
    state.window = state.platform->createWindow(
        *state.pool,
        {
            .width = pixelWidth,
            .height = pixelHeight,
        }
    );
    state.setGlyphSize(glyphWidth, glyphHeight);
    state.resize(pixelWidth, pixelHeight);
    VtermHeadlessImpl* result = state.pool->make<VtermHeadlessImpl>(state);
    Output* const sink = ptyCapture != nullptr ? ptyCapture : createNullOutput(state.pool);
    Vterm* const vterm = Vterm::create(*state.pool, state, *state.pool->make<OutputPtyHandle>(state, *sink), traceFactory);
    result->terminal_ = vterm;
    state.resizedListeners.pushBack(state.pool->make<CallHeadlessResize>(vterm));
    state.fontChangedListeners.pushBack(state.pool->make<CallHeadlessFontChanged>(vterm));
    return result;
}
