/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "input_router.h"

#include "composer.h"
#include "input_sink.h"

#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    struct InputRouter final: public InputSink {
        explicit InputRouter(Composer& composer);

        bool key(const KeyInput& input) override;
        bool text(const TextInput& input) override;
        bool pointerMotion(const PointerMotionInput& input) override;
        bool pointerButton(const PointerButtonInput& input) override;
        bool scroll(const ScrollInput& input) override;
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

bool InputRouter::key(const KeyInput& input) {
    for (IntrusiveNode* node = composer.inputSinks.mutFront(); node != composer.inputSinks.mutEnd();) {
        InputSink* const sink = static_cast<InputSink*>(node);
        node = node->next;
        if (sink->key(input)) {
            return true;
        }
    }
    return false;
}

bool InputRouter::text(const TextInput& input) {
    for (IntrusiveNode* node = composer.inputSinks.mutFront(); node != composer.inputSinks.mutEnd();) {
        InputSink* const sink = static_cast<InputSink*>(node);
        node = node->next;
        if (sink->text(input)) {
            return true;
        }
    }
    return false;
}

bool InputRouter::pointerMotion(const PointerMotionInput& input) {
    for (IntrusiveNode* node = composer.inputSinks.mutFront(); node != composer.inputSinks.mutEnd();) {
        InputSink* const sink = static_cast<InputSink*>(node);
        node = node->next;
        if (sink->pointerMotion(input)) {
            return true;
        }
    }
    return false;
}

bool InputRouter::pointerButton(const PointerButtonInput& input) {
    for (IntrusiveNode* node = composer.inputSinks.mutFront(); node != composer.inputSinks.mutEnd();) {
        InputSink* const sink = static_cast<InputSink*>(node);
        node = node->next;
        if (sink->pointerButton(input)) {
            return true;
        }
    }
    return false;
}

bool InputRouter::scroll(const ScrollInput& input) {
    for (IntrusiveNode* node = composer.inputSinks.mutFront(); node != composer.inputSinks.mutEnd();) {
        InputSink* const sink = static_cast<InputSink*>(node);
        node = node->next;
        if (sink->scroll(input)) {
            return true;
        }
    }
    return false;
}

void InputRouter::focus(bool focused) {
    for (IntrusiveNode* node = composer.inputSinks.mutFront(); node != composer.inputSinks.mutEnd();) {
        InputSink* const sink = static_cast<InputSink*>(node);
        node = node->next;
        sink->focus(focused);
    }
}

void InputRouter::pointerPresence(bool present) {
    for (IntrusiveNode* node = composer.inputSinks.mutFront(); node != composer.inputSinks.mutEnd();) {
        InputSink* const sink = static_cast<InputSink*>(node);
        node = node->next;
        sink->pointerPresence(present);
    }
}

void InputRouter::flush() {
    for (IntrusiveNode* node = composer.inputSinks.mutFront(); node != composer.inputSinks.mutEnd();) {
        InputSink* const sink = static_cast<InputSink*>(node);
        node = node->next;
        sink->flush();
    }
}

InputSink* createInputRouter(Composer& composer) {
    return composer.pool->make<InputRouter>(composer);
}
