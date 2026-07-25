/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "input_bindings.h"

#include "composer.h"
#include "input_sink.h"
#include "keyboard.h"
#include "listener.h"

#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

namespace {
    struct CaptureSink final: public InputSink {
        bool key(const KeyInput& input) override;
        bool text(const TextInput& input) override;
        bool pointerMotion(const PointerMotionInput& input) override;
        bool pointerButton(const PointerButtonInput& input) override;
        bool scroll(const ScrollInput& input) override;
        void focus(bool focused) override;
        void pointerPresence(bool present) override;
        void flush() override;

        bool consume = false;
        bool unlinkOnKey = false;
        KeyInput lastKey;
        TextInput lastText;
        PointerMotionInput lastMotion;
        PointerButtonInput lastButton;
        ScrollInput lastScroll;
        size_t keys = 0;
        size_t texts = 0;
        size_t motions = 0;
        size_t buttons = 0;
        size_t scrolls = 0;
        size_t focuses = 0;
        size_t presences = 0;
        size_t flushes = 0;
        bool focused = false;
        bool present = false;
    };

    struct CountBinding final: public Listener {
        void onListen(void*) override;

        size_t calls = 0;
    };
}

bool CaptureSink::key(const KeyInput& input) {
    lastKey = input;
    ++keys;
    if (unlinkOnKey) {
        unlink();
    }
    return consume;
}

bool CaptureSink::text(const TextInput& input) {
    lastText = input;
    ++texts;
    return consume;
}

bool CaptureSink::pointerMotion(const PointerMotionInput& input) {
    lastMotion = input;
    ++motions;
    return consume;
}

bool CaptureSink::pointerButton(const PointerButtonInput& input) {
    lastButton = input;
    ++buttons;
    return consume;
}

bool CaptureSink::scroll(const ScrollInput& input) {
    lastScroll = input;
    ++scrolls;
    return consume;
}

void CaptureSink::focus(bool focused_) {
    focused = focused_;
    ++focuses;
}

void CaptureSink::pointerPresence(bool present_) {
    present = present_;
    ++presences;
}

void CaptureSink::flush() {
    ++flushes;
}

void CountBinding::onListen(void*) {
    ++calls;
}

STD_TEST_SUITE(Keyboard) {
    STD_TEST(MapsAlphabeticControlCharacters) {
        u8 character = 0;

        for (int key = 'A'; key <= 'Z'; ++key) {
            STD_INSIST(controlCharacter(key, false, character));
            STD_INSIST(character == key - 'A' + 1);
        }
    }

    STD_TEST(MapsPunctuationControlAliases) {
        u8 character = 0;

        const struct {
            int key;
            bool shifted;
            u8 expected;
        } cases[] = {
            {' ', false, 0},
            {'2', false, 0},
            {'3', false, 27},
            {'[', false, 27},
            {'4', false, 28},
            {'\\', false, 28},
            {'5', false, 29},
            {']', false, 29},
            {'6', false, 30},
            {'7', false, 31},
            {'8', false, 127},
            {'-', false, '-'},
            {'-', true, 31},
            {'/', false, 31},
            {'/', true, 127},
        };

        for (const auto& item : cases) {
            STD_INSIST(controlCharacter(item.key, item.shifted, character));
            STD_INSIST(character == item.expected);
        }
    }

    STD_TEST(PassesOtherAsciiAndRejectsNonAscii) {
        u8 character = 0;

        STD_INSIST(controlCharacter('a', false, character));
        STD_INSIST(character == 'a');
        STD_INSIST(!controlCharacter(0, false, character));
        STD_INSIST(!controlCharacter(128, false, character));
    }
}

STD_TEST_SUITE(InputRouter) {
    STD_TEST(StopsAtFirstConsumingSink) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CaptureSink first;
        CaptureSink second;
        first.consume = true;
        composer.inputSinks.pushBack(&first);
        composer.inputSinks.pushBack(&second);

        STD_INSIST(composer.input->key({InputKey::Enter}));
        STD_INSIST(first.keys == 1);
        STD_INSIST(second.keys == 0);
    }

    STD_TEST(ContinuesAfterSinkRemovesItself) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CaptureSink removing;
        CaptureSink trailing;
        removing.unlinkOnKey = true;
        composer.inputSinks.pushBack(&removing);
        composer.inputSinks.pushBack(&trailing);

        STD_INSIST(!composer.input->key({InputKey::Enter}));
        STD_INSIST(removing.keys == 1);
        STD_INSIST(trailing.keys == 1);

        composer.input->key({InputKey::Enter});
        STD_INSIST(removing.keys == 1);
        STD_INSIST(trailing.keys == 2);
    }

    STD_TEST(RoutesEveryInputShape) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CaptureSink sink;
        composer.inputSinks.pushBack(&sink);

        composer.input->text({0x20ac, InputAlt});
        composer.input->pointerMotion({12, 34, InputShift});
        composer.input->pointerButton({PointerButton::Middle, true, 12, 34, InputControl, 1.5});
        composer.input->scroll({1.25, -2.5, 12, 34, InputSuper});

        STD_INSIST(sink.texts == 1);
        STD_INSIST(sink.lastText.codepoint == 0x20ac);
        STD_INSIST(sink.motions == 1);
        STD_INSIST(sink.lastMotion.pixelY == 34);
        STD_INSIST(sink.buttons == 1);
        STD_INSIST(sink.lastButton.button == PointerButton::Middle);
        STD_INSIST(sink.scrolls == 1);
        STD_INSIST(sink.lastScroll.y == -2.5);
    }

    STD_TEST(BroadcastsStateAndFlushEvents) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CaptureSink first;
        CaptureSink second;
        composer.inputSinks.pushBack(&first);
        composer.inputSinks.pushBack(&second);

        composer.input->focus(true);
        composer.input->pointerPresence(true);
        composer.input->flush();

        STD_INSIST(first.focuses == 1 && second.focuses == 1);
        STD_INSIST(first.focused && second.focused);
        STD_INSIST(first.presences == 1 && second.presences == 1);
        STD_INSIST(first.present && second.present);
        STD_INSIST(first.flushes == 1 && second.flushes == 1);
    }
}

STD_TEST_SUITE(InputBindings) {
    STD_TEST(MatchesNormalizedModifiersAndPublishes) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        IntrusiveList listeners;
        CountBinding listener;
        listeners.pushBack(&listener);
        composer.inputBindings->add({InputKey::Printable, InputControl, 'a', 'a'}, &listeners);

        const bool consumed = composer.input->key({
            .key = InputKey::Printable,
            .action = InputAction::Press,
            .modifiers = InputControl | InputCapsLock | InputNumLock,
            .baseCodepoint = 'a',
        });

        STD_INSIST(consumed);
        STD_INSIST(listener.calls == 1);
        STD_INSIST(composer.input->text({'a', InputControl}));
        STD_INSIST(composer.input->key({InputKey::Printable, InputAction::Release, InputControl, 0, 'a'}));
    }

    STD_TEST(DoesNotConsumeMismatchedBinding) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        IntrusiveList listeners;
        CountBinding listener;
        listeners.pushBack(&listener);
        composer.inputBindings->add({InputKey::Printable, InputControl, 'a', 0}, &listeners);

        STD_INSIST(!composer.input->key({InputKey::Printable, InputAction::Press, InputShift, 0, 'a'}));
        STD_INSIST(!composer.input->key({InputKey::Printable, InputAction::Press, InputControl, 0, 'b'}));
        STD_INSIST(listener.calls == 0);
    }

    STD_TEST(TracksRepeatedPendingText) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        IntrusiveList listeners;
        CountBinding listener;
        listeners.pushBack(&listener);
        composer.inputBindings->add({InputKey::Printable, InputControl, '=', '+'}, &listeners);
        const KeyInput input{InputKey::Printable, InputAction::Press, InputControl, '=', '='};

        STD_INSIST(composer.input->key(input));
        STD_INSIST(composer.input->key(input));
        STD_INSIST(listener.calls == 2);
        STD_INSIST(composer.input->text({'+', InputControl}));
        STD_INSIST(composer.input->text({'+', InputControl}));
        STD_INSIST(!composer.input->text({'+', InputControl}));
    }

    STD_TEST(FlushDropsPendingTextButReleaseRemainsConsumed) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        IntrusiveList listeners;
        CountBinding listener;
        listeners.pushBack(&listener);
        composer.inputBindings->add({InputKey::Printable, InputControl, '=', '+'}, &listeners);
        composer.input->key({InputKey::Printable, InputAction::Press, InputControl, '=', '='});

        composer.input->flush();

        STD_INSIST(!composer.input->text({'+', InputControl}));
        STD_INSIST(composer.input->key({InputKey::Printable, InputAction::Release, InputControl, '=', '='}));
    }

    STD_TEST(FocusLossClearsConsumedAndPendingState) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        IntrusiveList listeners;
        CountBinding listener;
        listeners.pushBack(&listener);
        composer.inputBindings->add({InputKey::Printable, InputControl, '=', '+'}, &listeners);
        composer.input->key({InputKey::Printable, InputAction::Press, InputControl, '=', '='});

        composer.input->focus(false);

        STD_INSIST(!composer.input->text({'+', InputControl}));
        STD_INSIST(!composer.input->key({InputKey::Printable, InputAction::Release, InputControl, '=', '='}));
    }
}
