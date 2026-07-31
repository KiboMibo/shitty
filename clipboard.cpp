/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "clipboard.h"

#include "composer.h"

#include <plt/window.h>

#include <std/ios/output.h>
#include <std/lib/list.h>
#include <std/mem/obj_pool.h>
#include <std/mem/small_obj_allocator.h>

#include <new>

using namespace stl;

namespace {
    struct ClipboardImpl;

    struct ClipboardOutput final: public plt::ClipboardRead, public IntrusiveNode {
        ClipboardOutput(ClipboardImpl* clipboard, plt::Clipboard* source, Output* output);

        void operator delete(ClipboardOutput* read, std::destroying_delete_t) noexcept;

        bool data(StringView chunk) override;
        void done(bool success) override;
        void cancel();
        void complete(bool success);

        ClipboardImpl* clipboard;
        plt::Clipboard* source;
        Output* output;
    };

    struct ClipboardImpl final: public Clipboard {
        ClipboardImpl(Composer& composer, plt::Window& window);
        ~ClipboardImpl();

        void readPrimary(Output* output) override;
        void readClipboard(Output* output) override;
        void writePrimary(StringView content) override;
        void writeClipboard(StringView content) override;

        void cancelReads();

        Composer& composer;
        plt::Window& window;
        IntrusiveList reads;
    };
}

ClipboardOutput::ClipboardOutput(ClipboardImpl* clipboard_, plt::Clipboard* source_, Output* output_)
    : clipboard(clipboard_)
    , source(source_)
    , output(output_)
{
}

void ClipboardOutput::operator delete(ClipboardOutput* read, std::destroying_delete_t) noexcept {
    read->clipboard->composer.smallObjects->release(read);
}

bool ClipboardOutput::data(StringView chunk) {
    output->write(chunk.data(), chunk.length());
    return true;
}

void ClipboardOutput::done(bool success) {
    complete(success);
}

void ClipboardOutput::cancel() {
    source->cancel(*this);
    complete(false);
}

void ClipboardOutput::complete(bool success) {
    unlink();
    Output* const completed = output;
    output = nullptr;
    if (success) {
        completed->finish();
    }
    delete completed;
    delete this;
}

ClipboardImpl::ClipboardImpl(Composer& composer_, plt::Window& window_)
    : composer(composer_)
    , window(window_)
{
}

ClipboardImpl::~ClipboardImpl() {
    cancelReads();
}

void ClipboardImpl::readPrimary(Output* output) {
    plt::Clipboard* const source = window.primary();
    ClipboardOutput* const read = composer.smallObjects->make<ClipboardOutput>(this, source, output);
    reads.pushBack(read);
    source->read(*read);
}

void ClipboardImpl::readClipboard(Output* output) {
    plt::Clipboard* const source = window.secondary();
    ClipboardOutput* const read = composer.smallObjects->make<ClipboardOutput>(this, source, output);
    reads.pushBack(read);
    source->read(*read);
}

void ClipboardImpl::writePrimary(StringView content) {
    window.primary()->write(content);
}

void ClipboardImpl::writeClipboard(StringView content) {
    window.secondary()->write(content);
}

void ClipboardImpl::cancelReads() {
    while (!reads.empty()) {
        static_cast<ClipboardOutput*>(reads.mutFront())->cancel();
    }
}

Clipboard* Clipboard::create(Composer& composer, plt::Window& window) {
    return composer.pool->make<ClipboardImpl>(composer, window);
}
