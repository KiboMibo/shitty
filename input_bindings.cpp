/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "input_bindings.h"

#include "composer.h"
#include "input_sink.h"
#include "listener.h"

#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

#include <cassert>

namespace stl {}

using namespace stl;

namespace {
    struct RegisteredBinding {
        InputBinding input;
        IntrusiveList* listeners = nullptr;
        unsigned pendingText = 0;
        bool consumed = false;
    };

    struct InputBindingsImpl final: public InputBindings, public InputSink {
        explicit InputBindingsImpl(Composer& composer);

        void add(const InputBinding& binding, IntrusiveList* listeners) override;
        bool key(const KeyInput& input) override;
        bool text(const TextInput& input) override;
        bool pointerMotion(const PointerMotionInput& input) override;
        bool pointerButton(const PointerButtonInput& input) override;
        bool scroll(const ScrollInput& input) override;
        void focus(bool focused) override;
        void pointerPresence(bool present) override;
        void flush() override;

        static void publish(IntrusiveList& listeners);
        static u16 normalizedModifiers(u16 modifiers);
        RegisteredBinding* find(const KeyInput& input);

        Composer& composer_;
        Vector<RegisteredBinding> bindings_;
    };
}

InputBindingsImpl::InputBindingsImpl(Composer& composer)
    : composer_(composer)
{
    composer_.inputSinks.pushBack(this);
}

void InputBindingsImpl::add(const InputBinding& binding, IntrusiveList* listeners) {
    assert(listeners != nullptr);
    bindings_.pushBack({binding, listeners});
}

void InputBindingsImpl::publish(IntrusiveList& listeners) {
    for (IntrusiveNode* node = listeners.mutFront(); node != listeners.mutEnd();) {
        Listener* const listener = static_cast<Listener*>(node);
        node = node->next;
        listener->onListen();
    }
}

u16 InputBindingsImpl::normalizedModifiers(u16 modifiers) {
    return modifiers & ~(InputCapsLock | InputNumLock);
}

RegisteredBinding* InputBindingsImpl::find(const KeyInput& input) {
    const u16 modifiers = normalizedModifiers(input.modifiers);
    for (RegisteredBinding* binding = bindings_.mutBegin(); binding != bindings_.mutEnd(); ++binding) {
        if (binding->input.key == input.key && binding->input.baseCodepoint == input.baseCodepoint && binding->input.modifiers == modifiers) {
            return binding;
        }
    }
    return nullptr;
}

bool InputBindingsImpl::key(const KeyInput& input) {
    if (input.action == InputAction::Release) {
        for (RegisteredBinding* binding = bindings_.mutBegin(); binding != bindings_.mutEnd(); ++binding) {
            if (binding->consumed && binding->input.key == input.key && binding->input.baseCodepoint == input.baseCodepoint) {
                binding->consumed = false;
                binding->pendingText = 0;
                return true;
            }
        }
        return false;
    }
    RegisteredBinding* const binding = find(input);
    if (binding == nullptr) {
        return false;
    }

    binding->consumed = true;
    if (binding->input.textCodepoint != 0) {
        ++binding->pendingText;
    }
    publish(*binding->listeners);
    return true;
}

bool InputBindingsImpl::text(const TextInput& input) {
    for (RegisteredBinding* binding = bindings_.mutBegin(); binding != bindings_.mutEnd(); ++binding) {
        if (binding->pendingText != 0 && binding->input.textCodepoint == input.codepoint) {
            --binding->pendingText;
            return true;
        }
    }
    return false;
}

bool InputBindingsImpl::pointerMotion(const PointerMotionInput&) {
    return false;
}

bool InputBindingsImpl::pointerButton(const PointerButtonInput&) {
    return false;
}

bool InputBindingsImpl::scroll(const ScrollInput&) {
    return false;
}

void InputBindingsImpl::focus(bool focused) {
    if (focused) {
        return;
    }
    for (RegisteredBinding* binding = bindings_.mutBegin(); binding != bindings_.mutEnd(); ++binding) {
        binding->pendingText = 0;
        binding->consumed = false;
    }
}

void InputBindingsImpl::pointerPresence(bool) {
}

void InputBindingsImpl::flush() {
    for (RegisteredBinding* binding = bindings_.mutBegin(); binding != bindings_.mutEnd(); ++binding) {
        binding->pendingText = 0;
    }
}

InputBindings* InputBindings::create(Composer& composer) {
    return composer.pool->make<InputBindingsImpl>(composer);
}
