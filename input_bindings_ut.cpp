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
}

void CountBinding::onListen(void*) {
    ++calls;
}

STD_TEST_SUITE(InputBindings) {
    STD_TEST(MatchesNormalizedModifiersAndPublishes) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        IntrusiveList listeners;
        CountBinding listener;
        listeners.pushBack(&listener);
        composer.inputBindings->add({InputKey::Printable, InputControl, 'a', 'a'}, &listeners);

        const bool consumed = composer.inputBindings->key({
            .key = InputKey::Printable,
            .action = InputAction::Press,
            .modifiers = InputControl | InputCapsLock | InputNumLock,
            .baseCodepoint = 'a',
        });

        STD_INSIST(consumed);
        STD_INSIST(listener.calls == 1);
        STD_INSIST(composer.inputBindings->text({'a', InputControl}));
        STD_INSIST(composer.inputBindings->key({InputKey::Printable, InputAction::Release, InputControl, 0, 'a'}));
    }

    STD_TEST(DoesNotConsumeMismatchedBinding) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        IntrusiveList listeners;
        CountBinding listener;
        listeners.pushBack(&listener);
        composer.inputBindings->add({InputKey::Printable, InputControl, 'a', 0}, &listeners);

        STD_INSIST(!composer.inputBindings->key({InputKey::Printable, InputAction::Press, InputShift, 0, 'a'}));
        STD_INSIST(!composer.inputBindings->key({InputKey::Printable, InputAction::Press, InputControl, 0, 'b'}));
        STD_INSIST(listener.calls == 0);
    }

    STD_TEST(TracksRepeatedPendingText) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        IntrusiveList listeners;
        CountBinding listener;
        listeners.pushBack(&listener);
        composer.inputBindings->add({InputKey::Printable, InputControl, '=', '+'}, &listeners);
        const KeyInput input{InputKey::Printable, InputAction::Press, InputControl, '=', '='};

        STD_INSIST(composer.inputBindings->key(input));
        STD_INSIST(composer.inputBindings->key(input));
        STD_INSIST(listener.calls == 2);
        STD_INSIST(composer.inputBindings->text({'+', InputControl}));
        STD_INSIST(composer.inputBindings->text({'+', InputControl}));
        STD_INSIST(!composer.inputBindings->text({'+', InputControl}));
    }

    STD_TEST(FlushDropsPendingTextButReleaseRemainsConsumed) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        IntrusiveList listeners;
        CountBinding listener;
        listeners.pushBack(&listener);
        composer.inputBindings->add({InputKey::Printable, InputControl, '=', '+'}, &listeners);
        composer.input->key({InputKey::Printable, InputAction::Press, InputControl, '=', '='});

        composer.input->flush();

        STD_INSIST(!composer.inputBindings->text({'+', InputControl}));
        STD_INSIST(composer.inputBindings->key({InputKey::Printable, InputAction::Release, InputControl, '=', '='}));
    }

    STD_TEST(FocusLossClearsConsumedAndPendingState) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        IntrusiveList listeners;
        CountBinding listener;
        listeners.pushBack(&listener);
        composer.inputBindings->add({InputKey::Printable, InputControl, '=', '+'}, &listeners);
        composer.input->key({InputKey::Printable, InputAction::Press, InputControl, '=', '='});

        composer.input->focus(false);

        STD_INSIST(!composer.inputBindings->text({'+', InputControl}));
        STD_INSIST(!composer.inputBindings->key({InputKey::Printable, InputAction::Release, InputControl, '=', '='}));
    }
}
