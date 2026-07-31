/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "input_router.h"

#include "composer.h"
#include "input_handler.h"
#include "vterm.h"

#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    struct InputRouter final: public plt::InputSink {
        explicit InputRouter(Composer& composer);

        void key(const plt::KeyInput& input) override;
        void text(const plt::TextInput& input) override;
        void preedit(stl::StringView text, i32 cursorBegin, i32 cursorEnd) override;
        void drop(stl::StringView text) override;
        void dropPath(stl::StringView path) override;
        void pointerMotion(const plt::PointerMotionInput& input) override;
        void pointerButton(const plt::PointerButtonInput& input) override;
        void scroll(const plt::ScrollInput& input) override;
        void focus(bool focused) override;
        void pointerPresence(bool present) override;
        void flush() override;

        Composer& composer;
    };
}

InputRouter::InputRouter(Composer& composer_)
    : composer(composer_)
{
}

void InputRouter::key(const plt::KeyInput& input) {
    for (IntrusiveNode* node = composer.inputHandlers.mutFront(); node != composer.inputHandlers.mutEnd();) {
        InputHandler* const handler = static_cast<InputHandler*>(node);
        node = node->next;
        if (handler->key(input)) {
            return;
        }
    }
}

void InputRouter::text(const plt::TextInput& input) {
    for (IntrusiveNode* node = composer.inputHandlers.mutFront(); node != composer.inputHandlers.mutEnd();) {
        InputHandler* const handler = static_cast<InputHandler*>(node);
        node = node->next;
        if (handler->text(input)) {
            return;
        }
    }
}

void InputRouter::preedit(stl::StringView text, i32 cursorBegin, i32 cursorEnd) {
    if (composer.vterm != nullptr) {
        composer.vterm->preedit(text, cursorBegin, cursorEnd);
    }
}

void InputRouter::drop(stl::StringView text) {
    if (composer.vterm != nullptr) {
        composer.vterm->drop(text);
    }
}

void InputRouter::dropPath(stl::StringView path) {
    if (composer.vterm != nullptr) {
        composer.vterm->dropPath(path);
    }
}

void InputRouter::pointerMotion(const plt::PointerMotionInput& input) {
    for (IntrusiveNode* node = composer.inputHandlers.mutFront(); node != composer.inputHandlers.mutEnd();) {
        InputHandler* const handler = static_cast<InputHandler*>(node);
        node = node->next;
        if (handler->pointerMotion(input)) {
            return;
        }
    }
}

void InputRouter::pointerButton(const plt::PointerButtonInput& input) {
    for (IntrusiveNode* node = composer.inputHandlers.mutFront(); node != composer.inputHandlers.mutEnd();) {
        InputHandler* const handler = static_cast<InputHandler*>(node);
        node = node->next;
        if (handler->pointerButton(input)) {
            return;
        }
    }
}

void InputRouter::scroll(const plt::ScrollInput& input) {
    for (IntrusiveNode* node = composer.inputHandlers.mutFront(); node != composer.inputHandlers.mutEnd();) {
        InputHandler* const handler = static_cast<InputHandler*>(node);
        node = node->next;
        if (handler->scroll(input)) {
            return;
        }
    }
}

void InputRouter::focus(bool focused) {
    for (IntrusiveNode* node = composer.inputHandlers.mutFront(); node != composer.inputHandlers.mutEnd();) {
        InputHandler* const handler = static_cast<InputHandler*>(node);
        node = node->next;
        handler->focus(focused);
    }
}

void InputRouter::pointerPresence(bool present) {
    for (IntrusiveNode* node = composer.inputHandlers.mutFront(); node != composer.inputHandlers.mutEnd();) {
        InputHandler* const handler = static_cast<InputHandler*>(node);
        node = node->next;
        handler->pointerPresence(present);
    }
}

void InputRouter::flush() {
    for (IntrusiveNode* node = composer.inputHandlers.mutFront(); node != composer.inputHandlers.mutEnd();) {
        InputHandler* const handler = static_cast<InputHandler*>(node);
        node = node->next;
        handler->flush();
    }
}

plt::InputSink* createInputRouter(Composer& composer) {
    return composer.pool->make<InputRouter>(composer);
}
