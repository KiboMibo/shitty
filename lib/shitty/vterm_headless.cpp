/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "vterm_headless.h"

#include "pty.h"
#include "vterm.h"
#include "options.h"
#include "composer.h"
#include "pane_layout.h"
#include "grid_geometry.h"

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
        CallHeadlessResize(Composer& composer, Vterm* terminal);

        void onListen(void*) override;

        // A8: the headless host shows one terminal filling its surface,
        // so the pane it hands over is the window's - but it has to hand
        // one over, because the terminal no longer reads the composer for
        // its grid.
        Composer& composer;
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

CallHeadlessResize::CallHeadlessResize(Composer& composer_, Vterm* terminal_)
    : composer(composer_)
    , terminal(terminal_)
{
}

void CallHeadlessResize::onListen(void*) {
    terminal->paneResized(windowPane(composer));
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

VtermHeadless* VtermHeadless::create(Composer& composer, VtermTraceFactory* traceFactory, Output* ptyCapture) {
    constexpr u16 columns = 80;
    constexpr u16 rows = 24;
    constexpr u16 glyphWidth = 1;
    constexpr u16 glyphHeight = 1;
    const Insets insets = composer.contentInsets();
    const u16 pixelWidth = (u16)(gridPixelWidth(columns, insets, glyphWidth));
    const u16 pixelHeight = (u16)(gridPixelHeight(rows, insets, glyphHeight));

    composer.vt.platform = plt::createHeadlessPlatform(*composer.pool);
    composer.vt.window = composer.vt.platform->createWindow(
        *composer.pool,
        {
            .width = pixelWidth,
            .height = pixelHeight,
        }
    );
    composer.vt.setGlyphSize(glyphWidth, glyphHeight);
    composer.resize(pixelWidth, pixelHeight);
    VtermHeadlessImpl* result = composer.pool->make<VtermHeadlessImpl>(composer.vt);
    Output* const sink = ptyCapture != nullptr ? ptyCapture : createNullOutput(composer.pool);
    Vterm* const vterm = Vterm::create(*composer.pool, composer, windowPane(composer), *composer.pool->make<OutputPtyHandle>(composer.vt, *sink), traceFactory);
    result->terminal_ = vterm;
    composer.vt.resizedListeners.pushBack(composer.pool->make<CallHeadlessResize>(composer, vterm));
    composer.vt.fontChangedListeners.pushBack(composer.pool->make<CallHeadlessFontChanged>(vterm));
    return result;
}
