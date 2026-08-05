/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "input_bindings.h"

#include "composer.h"
#include "listener.h"

#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;
using namespace plt;

namespace {
    struct CountBinding final: public Listener {
        void onListen(void*) override;

        size_t calls = 0;
    };

#if defined(__APPLE__)
    static constexpr u16 copyModifiers = InputSuper;
    static constexpr u16 inactiveCopyModifiers = InputControl | InputShift;
    static constexpr u16 incFontModifiers = InputSuper;
    static constexpr u32 incFontText = 0;
    static constexpr u16 tabModifiers = InputSuper;
    static constexpr u16 tabSwitchModifiers = InputSuper | InputShift;
#elif defined(__linux__)
    static constexpr u16 copyModifiers = InputControl | InputShift;
    static constexpr u16 inactiveCopyModifiers = InputSuper;
    static constexpr u16 incFontModifiers = InputControl | InputShift;
    static constexpr u32 incFontText = '+';
    static constexpr u16 tabModifiers = InputControl | InputShift;
    static constexpr u16 tabSwitchModifiers = InputControl | InputShift;
#else
    #error Unsupported platform
#endif
}

void CountBinding::onListen(void*) {
    ++calls;
}

STD_TEST_SUITE(InputBindings) {
    STD_TEST(MatchesNormalizedModifiersAndPublishes) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CountBinding listener;
        composer.copyListeners.pushBack(&listener);

        const bool consumed = composer.inputBindings->key({
            .key = InputKey::Printable,
            .action = InputAction::Press,
            .modifiers = copyModifiers | InputCapsLock | InputNumLock,
            .baseCodepoint = 'c',
        });

        STD_INSIST(consumed);
        STD_INSIST(listener.calls == 1);
        STD_INSIST(!composer.inputBindings->text({'c', copyModifiers}));
        STD_INSIST(composer.inputBindings->key({InputKey::Printable, InputAction::Release, copyModifiers, 0, 'c'}));
    }

    STD_TEST(DoesNotConsumeMismatchedBinding) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CountBinding listener;
        composer.copyListeners.pushBack(&listener);

        STD_INSIST(!composer.inputBindings->key({InputKey::Printable, InputAction::Press, inactiveCopyModifiers, 0, 'c'}));
        STD_INSIST(!composer.inputBindings->key({InputKey::Printable, InputAction::Press, copyModifiers, 0, 'b'}));
        STD_INSIST(listener.calls == 0);
    }

    STD_TEST(TracksRepeatedPlatformBinding) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        IntrusiveList listeners;
        CountBinding listener;
        listeners.pushBack(&listener);
        composer.inputBindings->add(InputActions::IncFontSize, &listeners);
        const KeyInput input{InputKey::Printable, InputAction::Press, incFontModifiers, '=', '='};

        STD_INSIST(composer.inputBindings->key(input));
        STD_INSIST(composer.inputBindings->key(input));
        STD_INSIST(listener.calls == 2);
        if (incFontText != 0) {
            STD_INSIST(composer.inputBindings->text({incFontText, incFontModifiers}));
            STD_INSIST(composer.inputBindings->text({incFontText, incFontModifiers}));
        }
        STD_INSIST(!composer.inputBindings->text({'+', incFontModifiers}));
    }

    STD_TEST(FlushDropsPendingTextButReleaseRemainsConsumed) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        IntrusiveList listeners;
        CountBinding listener;
        listeners.pushBack(&listener);
        composer.inputBindings->add(InputActions::IncFontSize, &listeners);
        composer.input->key({InputKey::Printable, InputAction::Press, incFontModifiers, '=', '='});

        composer.input->flush();

        STD_INSIST(!composer.inputBindings->text({'+', incFontModifiers}));
        STD_INSIST(composer.inputBindings->key({InputKey::Printable, InputAction::Release, incFontModifiers, '=', '='}));
    }

    STD_TEST(FocusLossClearsConsumedAndPendingState) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        IntrusiveList listeners;
        CountBinding listener;
        listeners.pushBack(&listener);
        composer.inputBindings->add(InputActions::IncFontSize, &listeners);
        composer.input->key({InputKey::Printable, InputAction::Press, incFontModifiers, '=', '='});

        composer.input->focus(false);

        STD_INSIST(!composer.inputBindings->text({'+', incFontModifiers}));
        STD_INSIST(!composer.inputBindings->key({InputKey::Printable, InputAction::Release, incFontModifiers, '=', '='}));
    }

    // The tab chords must exist in both platform blocks: add() asserts it
    // found a row for the action, so a row present on one platform and
    // missing on the other trips registration on the platform that lacks
    // it rather than failing quietly.
    STD_TEST(TabActionsAreBoundOnThisPlatform) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CountBinding newTab;
        CountBinding nextTab;
        composer.newTabListeners.pushBack(&newTab);
        composer.nextTabListeners.pushBack(&nextTab);

        STD_INSIST(composer.inputBindings->key({InputKey::Printable, InputAction::Press, tabModifiers, 0, 't'}));
        STD_INSIST(newTab.calls == 1);
        STD_INSIST(composer.inputBindings->key({InputKey::Printable, InputAction::Press, tabSwitchModifiers, 0, ']'}));
        STD_INSIST(nextTab.calls == 1);
    }

    STD_TEST(ActionCanHaveMultiplePlatformBindings) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        CountBinding listener;
        composer.pastePrimaryListeners.pushBack(&listener);

        STD_INSIST(composer.inputBindings->key({InputKey::Insert, InputAction::Press, InputShift}));
        STD_INSIST(composer.inputBindings->key({InputKey::Insert, InputAction::Release, InputShift}));
        STD_INSIST(composer.inputBindings->key({InputKey::Keypad0, InputAction::Press, InputShift | InputCapsLock}));
        STD_INSIST(composer.inputBindings->key({InputKey::Keypad0, InputAction::Release, InputShift}));
        STD_INSIST(listener.calls == 2);
    }
}
