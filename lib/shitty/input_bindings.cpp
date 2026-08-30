/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "input_bindings.h"

#include "composer.h"
#include <lib/vterm/listener.h>
#include "options.h"

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
        // Rows of the -naturalEditing preset match only while the option
        // holds; it is read through the composer on every key, so a
        // config reload retunes the chords.
        bool naturalEditing = false;
        // The same, for -sidebarTabs and cmd+b. Without the option there
        // is no panel to show or hide, and a chord claimed to do nothing
        // is a chord taken away from whatever the application wanted it
        // for - the kitty keyboard protocol reports cmd+b to the program
        // running inside, and did so before this row existed
        // (test_keyboard.py picked that very chord as its Super-only
        // case, cmd+c being taken). Gated here rather than in the
        // sidebar module, so the key is not consumed in the first place.
        bool sidebarTabs = false;
        // The same, for -panes and the two split chords. cmd+d reaches
        // the program running inside under the kitty keyboard protocol
        // exactly as cmd+b does, so it stays the program's until the
        // option says a tab can be divided at all. This is the outer of
        // the two locks: splitFocused() refuses on the same option, so
        // there is no way in that skips the check.
        bool panes = false;
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
        // The splits. cmd+d and cmd+shift+d, and deliberately not cmd+h
        // for "split horizontally": the system Hide item eats cmd+h
        // before an application sees it.
        {InputActions::SplitVertical, {.key = InputKey::Printable, .modifiers = InputSuper, .baseCodepoint = 'd', .panes = true}},
        // Both forms, for the same reason the bracket chords below carry
        // two rows: the chord holds Shift, and whether a shifted key's
        // base codepoint keeps the shift is a question the frontends
        // answer differently. Cocoa's own correction is supposed to hand
        // back the unshifted 'd', but a single row makes the chord silent
        // if it ever does not - and silent is exactly how this one was
        // reported.
        {InputActions::SplitHorizontal, {.key = InputKey::Printable, .modifiers = InputSuper | InputShift, .baseCodepoint = 'd', .panes = true}},
        {InputActions::SplitHorizontal, {.key = InputKey::Printable, .modifiers = InputSuper | InputShift, .baseCodepoint = 'D', .panes = true}},
        // Both forms: the chord carries Shift and the frontends disagree
        // about whether the base codepoint of a shifted bracket keeps it.
        {InputActions::PrevTab, {InputKey::Printable, InputSuper | InputShift, '['}},
        {InputActions::PrevTab, {InputKey::Printable, InputSuper | InputShift, '{'}},
        {InputActions::NextTab, {InputKey::Printable, InputSuper | InputShift, ']'}},
        {InputActions::NextTab, {InputKey::Printable, InputSuper | InputShift, '}'}},
        // Cmd+arrows walk the tabs - unless the -naturalEditing preset
        // holds, whose line-start/end rows register first and shadow
        // these (the issue 82 reservation).
        {InputActions::PrevTab, {InputKey::Left, InputSuper}},
        {InputActions::NextTab, {InputKey::Right, InputSuper}},
        {InputActions::SelectTab1, {InputKey::Printable, InputSuper, '1'}},
        {InputActions::SelectTab2, {InputKey::Printable, InputSuper, '2'}},
        {InputActions::SelectTab3, {InputKey::Printable, InputSuper, '3'}},
        {InputActions::SelectTab4, {InputKey::Printable, InputSuper, '4'}},
        {InputActions::SelectTab5, {InputKey::Printable, InputSuper, '5'}},
        {InputActions::SelectTab6, {InputKey::Printable, InputSuper, '6'}},
        {InputActions::SelectTab7, {InputKey::Printable, InputSuper, '7'}},
        {InputActions::SelectTab8, {InputKey::Printable, InputSuper, '8'}},
        {InputActions::SelectTab9, {InputKey::Printable, InputSuper, '9'}},
        // Plain Ctrl+L stays the shell's, on both platforms.
        {InputActions::Clear, {InputKey::Printable, InputSuper, 'l'}},
        // The sidebar tab list. macOS only, like the module that answers
        // it (ui_sidebar_tabs.mm is in the darwin sources), and only
        // while -sidebarTabs is on: binding the chord where nothing can
        // act on it would only take a keystroke away for no gain.
        {InputActions::ToggleSidebar, {.key = InputKey::Printable, .modifiers = InputSuper, .baseCodepoint = 'b', .sidebarTabs = true}},
        // The -naturalEditing preset: the natural-text-editing chords of
        // Terminal.app, Ghostty's defaults and iTerm2's Natural Text
        // Editing preset. Not bound by default - the Command arrows stay
        // reserved for tab navigation - and, like every chord here, the
        // preset wins over whatever keyboard protocol the application
        // enabled.
        {InputActions::WordLeft, {InputKey::Left, InputAlt, 0, 0, true}},
        {InputActions::WordRight, {InputKey::Right, InputAlt, 0, 0, true}},
        {InputActions::LineStart, {InputKey::Left, InputSuper, 0, 0, true}},
        {InputActions::LineEnd, {InputKey::Right, InputSuper, 0, 0, true}},
        {InputActions::KillLine, {InputKey::Backspace, InputSuper, 0, 0, true}},
        {InputActions::EraseWord, {InputKey::Backspace, InputAlt, 0, 0, true}},
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
        {InputActions::Clear, {InputKey::Printable, InputControl | InputShift, 'l', 'L'}},
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
        explicit InputBindingsImpl(Composer& composer);

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

        Composer& composer_;
        Vector<RegisteredBinding> bindings_;
        bool registered_[(unsigned)(InputActions::Count)]{};
    };
}

InputBindingsImpl::InputBindingsImpl(Composer& composer)
    : composer_(composer)
{
}

void InputBindingsImpl::add(InputActions action, IntrusiveList* listeners) {
    STD_ASSERT(listeners != nullptr);
    const unsigned index = (unsigned)(action);
    STD_ASSERT(index < (unsigned)(InputActions::Count));
    STD_ASSERT(!registered_[index]);
    for (const ActionBinding& binding : defaultBindings) {
        if (binding.action == action) {
            bindings_.pushBack({binding.input, listeners});
        }
    }
    // An action may register with no chord on this platform: the
    // natural-editing preset only exists in the macOS table.
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

// A named key carries its whole identity in `key`; the codepoints only
// disambiguate printables. Cocoa reports arrows and friends with the
// function-key private-use codepoint in the base field, so comparing it
// for named keys would unmatch every such chord there.
static bool sameChordKey(const InputBinding& binding, InputKey key, u32 baseCodepoint) {
    if (binding.key != key) {
        return false;
    }
    return key != InputKey::Printable || binding.baseCodepoint == baseCodepoint;
}

RegisteredBinding* InputBindingsImpl::find(const KeyInput& input) {
    const u16 modifiers = normalizedModifiers(input.modifiers);
    for (RegisteredBinding* binding = bindings_.mutBegin(); binding != bindings_.mutEnd(); ++binding) {
        if (binding->input.naturalEditing && !composer_.opts->naturalEditing) {
            continue;
        }
        if (binding->input.sidebarTabs && !composer_.opts->sidebarTabs) {
            continue;
        }
        if (binding->input.panes && !composer_.opts->panes) {
            continue;
        }
        if (sameChordKey(binding->input, input.key, input.baseCodepoint) && binding->input.modifiers == modifiers) {
            return binding;
        }
    }
    return nullptr;
}

bool InputBindingsImpl::key(const KeyInput& input) {
    if (input.action == InputAction::Release) {
        bool consumed = false;
        for (RegisteredBinding* binding = bindings_.mutBegin(); binding != bindings_.mutEnd(); ++binding) {
            if (binding->consumed && sameChordKey(binding->input, input.key, input.baseCodepoint)) {
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
    return composer.pool->make<InputBindingsImpl>(composer);
}
