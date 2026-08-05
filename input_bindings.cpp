/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "input_bindings.h"

#include "composer.h"
#include "listener.h"

#include <std/dbg/assert.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

using namespace stl;
using namespace plt;

namespace {
    struct InputBinding {
        InputKey key = InputKey::Unknown;
        u16 modifiers = 0;
        u32 baseCodepoint = 0;
        u32 textCodepoint = 0;
    };

    struct ActionBinding {
        InputActions action;
        InputBinding input;
    };

    static constexpr ActionBinding defaultBindings[] = {
        {InputActions::PastePrimary, {InputKey::Insert, InputShift}},
        {InputActions::PastePrimary, {InputKey::Keypad0, InputShift}},
        {InputActions::PageUp, {InputKey::PageUp, InputShift}},
        {InputActions::PageDown, {InputKey::PageDown, InputShift}},
#if defined(__APPLE__)
        {InputActions::Copy, {InputKey::Printable, InputSuper, 'c'}},
        {InputActions::Paste, {InputKey::Printable, InputSuper, 'v'}},
        {InputActions::IncFontSize, {InputKey::Printable, InputSuper, '='}},
        {InputActions::IncFontSize, {InputKey::Printable, InputSuper | InputShift, '='}},
        {InputActions::DecFontSize, {InputKey::Printable, InputSuper, '-'}},
        {InputActions::ResetFontSize, {InputKey::Printable, InputSuper, '0'}},
        {InputActions::NewTab, {InputKey::Printable, InputSuper, 't'}},
        {InputActions::CloseTab, {InputKey::Printable, InputSuper, 'w'}},
        // Both forms: the chord carries Shift and the frontends disagree
        // about whether the base codepoint of a shifted bracket keeps it.
        {InputActions::PrevTab, {InputKey::Printable, InputSuper | InputShift, '['}},
        {InputActions::PrevTab, {InputKey::Printable, InputSuper | InputShift, '{'}},
        {InputActions::NextTab, {InputKey::Printable, InputSuper | InputShift, ']'}},
        {InputActions::NextTab, {InputKey::Printable, InputSuper | InputShift, '}'}},
#elif defined(__linux__)
        {InputActions::Copy, {InputKey::Printable, InputControl | InputShift, 'c'}},
        {InputActions::Paste, {InputKey::Printable, InputControl | InputShift, 'v'}},
        {InputActions::IncFontSize, {InputKey::Printable, InputControl | InputShift, '=', '+'}},
        {InputActions::DecFontSize, {InputKey::Printable, InputControl, '-', '-'}},
        {InputActions::ResetFontSize, {InputKey::Printable, InputControl, '0', '0'}},
        {InputActions::NewTab, {InputKey::Printable, InputControl | InputShift, 't'}},
        {InputActions::CloseTab, {InputKey::Printable, InputControl | InputShift, 'w'}},
        {InputActions::PrevTab, {InputKey::Printable, InputControl | InputShift, '['}},
        {InputActions::PrevTab, {InputKey::Printable, InputControl | InputShift, '{'}},
        {InputActions::NextTab, {InputKey::Printable, InputControl | InputShift, ']'}},
        {InputActions::NextTab, {InputKey::Printable, InputControl | InputShift, '}'}},
#else
    #error Unsupported platform
#endif
    };

    struct RegisteredBinding {
        InputBinding input;
        IntrusiveList* listeners = nullptr;
        unsigned pendingText = 0;
        bool consumed = false;
    };

    struct InputBindingsImpl final: public InputBindings {
        InputBindingsImpl();

        void add(InputActions action, IntrusiveList* listeners) override;
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

        Vector<RegisteredBinding> bindings_;
        bool registered_[(unsigned)(InputActions::Count)]{};
    };
}

InputBindingsImpl::InputBindingsImpl() {
}

void InputBindingsImpl::add(InputActions action, IntrusiveList* listeners) {
    STD_ASSERT(listeners != nullptr);
    const unsigned index = (unsigned)(action);
    STD_ASSERT(index < (unsigned)(InputActions::Count));
    STD_ASSERT(!registered_[index]);
    bool found = false;
    for (const ActionBinding& binding : defaultBindings) {
        if (binding.action == action) {
            bindings_.pushBack({binding.input, listeners});
            found = true;
        }
    }
    STD_ASSERT(found);
    registered_[index] = true;
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
        bool consumed = false;
        for (RegisteredBinding* binding = bindings_.mutBegin(); binding != bindings_.mutEnd(); ++binding) {
            if (binding->consumed && binding->input.key == input.key && binding->input.baseCodepoint == input.baseCodepoint) {
                binding->consumed = false;
                binding->pendingText = 0;
                consumed = true;
            }
        }
        return consumed;
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
    return composer.pool->make<InputBindingsImpl>();
}
