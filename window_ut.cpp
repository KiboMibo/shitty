/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "window.h"

#include "composer.h"
#include "input_sink.h"
#include "listener.h"
#include "options.h"
#include "test_mode.h"

#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

#define GLFW_INCLUDE_NONE
#include "third_party/glfw/include/GLFW/glfw3.h"

using namespace stl;

namespace {
    struct CaptureInput final: public InputSink {
        bool key(const KeyInput& input) override;
        bool text(const TextInput& input) override;
        bool pointerMotion(const PointerMotionInput&) override;
        bool pointerButton(const PointerButtonInput&) override;
        bool scroll(const ScrollInput&) override;
        void focus(bool) override;
        void pointerPresence(bool) override;
        void flush() override;

        KeyInput lastKey;
        TextInput lastText;
        size_t keys = 0;
        size_t texts = 0;
    };

    struct CountListener final: public Listener {
        void onListen(void*) override;

        size_t calls = 0;
    };
}

bool CaptureInput::key(const KeyInput& input) {
    lastKey = input;
    ++keys;
    return true;
}

bool CaptureInput::text(const TextInput& input) {
    lastText = input;
    ++texts;
    return true;
}

bool CaptureInput::pointerMotion(const PointerMotionInput&) {
    return false;
}

bool CaptureInput::pointerButton(const PointerButtonInput&) {
    return false;
}

bool CaptureInput::scroll(const ScrollInput&) {
    return false;
}

void CaptureInput::focus(bool) {
}

void CaptureInput::pointerPresence(bool) {
}

void CaptureInput::flush() {
}

void CountListener::onListen(void*) {
    ++calls;
}

STD_TEST_SUITE(Window) {
    STD_TEST(HeadlessRegistersOnlyWindowRole) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        Window* const window = Window::createHeadless(composer);

        STD_INSIST(composer.window == window);
        STD_INSIST(composer.clipboard == nullptr);
        STD_INSIST(composer.desktopActions == nullptr);
        STD_INSIST(window->testApi() != nullptr);
    }

    STD_TEST(HeadlessDispatchStops) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        Window& window = *Window::createHeadless(composer);

        STD_INSIST(!window.dispatchEvents());
    }

    STD_TEST(GlfwWindowDoesNotExposeTestInput) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        Window* const window = Window::create(composer);

        STD_INSIST(composer.window == window);
        STD_INSIST(window->testApi() == nullptr);
    }

    STD_TEST(HeadlessResizeUpdatesComposerGeometry) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        Window& window = *Window::createHeadless(composer);
        CountListener listener;
        composer.resizedListeners.pushBack(&listener);
        composer.setGlyphSize(8, 16);
        const u32 width = 2 * opts.border + 12 * composer.glyphWidth + 3;
        const u32 height = 2 * opts.border + 5 * composer.glyphHeight + 7;

        window.resizePixels(width, height);

        STD_INSIST(composer.pixelWidth == width);
        STD_INSIST(composer.pixelHeight == height);
        STD_INSIST(composer.columns == 12);
        STD_INSIST(composer.rows == 5);
        STD_INSIST(listener.calls == 1);
    }

    STD_TEST(HeadlessTranslatesPrintableAndTextInput) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        Window& window = *Window::createHeadless(composer);
        TestModeInput& input = *window.testApi();
        CaptureInput capture;
        composer.inputSinks.pushBack(&capture);

        input.testKeyEvent(GLFW_KEY_A, 0, GLFW_PRESS, GLFW_MOD_CONTROL | GLFW_MOD_CAPS_LOCK);
        input.testTextInput('a', GLFW_MOD_ALT | GLFW_MOD_NUM_LOCK);

        STD_INSIST(capture.keys == 1);
        STD_INSIST(capture.lastKey.key == InputKey::Printable);
        STD_INSIST(capture.lastKey.action == InputAction::Press);
        STD_INSIST(capture.lastKey.baseCodepoint == 'a');
        STD_INSIST(capture.lastKey.layoutCodepoint == 'a');
        STD_INSIST(capture.lastKey.modifiers == (InputControl | InputCapsLock));
        STD_INSIST(capture.texts == 1);
        STD_INSIST(capture.lastText.codepoint == 'a');
        STD_INSIST(capture.lastText.modifiers == (InputAlt | InputNumLock));
    }

    STD_TEST(HeadlessTranslatesSpecialKeysAndAltGraph) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        TestModeInput& input = *Window::createHeadless(composer)->testApi();
        CaptureInput capture;
        composer.inputSinks.pushBack(&capture);

        input.testKeyEvent(GLFW_KEY_UP, 0, GLFW_REPEAT, 0);

        STD_INSIST(capture.lastKey.key == InputKey::Up);
        STD_INSIST(capture.lastKey.action == InputAction::Repeat);
        STD_INSIST(capture.lastKey.baseCodepoint == 0);
        STD_INSIST(capture.lastKey.layoutCodepoint == 0);

        input.testKeyEvent(GLFW_KEY_RIGHT_ALT, 0, GLFW_PRESS, GLFW_MOD_ALT);

        STD_INSIST(capture.lastKey.key == InputKey::RightAlt);
        STD_INSIST((capture.lastKey.modifiers & InputAltGraph) != 0);
        STD_INSIST((capture.lastKey.modifiers & InputAlt) == 0);
    }

    STD_TEST(HeadlessRejectsUnknownKeyEvents) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        TestModeInput& input = *Window::createHeadless(composer)->testApi();
        CaptureInput capture;
        composer.inputSinks.pushBack(&capture);

        input.testKeyEvent(GLFW_KEY_UNKNOWN, 0, GLFW_PRESS, 0);
        input.testKeyEvent(GLFW_KEY_A, 0, 99, 0);
        input.testTextInput(0, 0);

        STD_INSIST(capture.keys == 0);
        STD_INSIST(capture.texts == 0);
    }

    STD_TEST(HeadlessPublishesContentScale) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        TestModeInput& input = *Window::createHeadless(composer)->testApi();
        CountListener listener;
        composer.contentScaleChangedListeners.pushBack(&listener);

        input.testContentScale(1.25f, 1.5f);
        input.testContentScale(1.5f, 1.25f);

        STD_INSIST(composer.contentScale == 1.5f);
        STD_INSIST(listener.calls == 1);
    }

    STD_TEST(WindowDestructionClearsComposerRole) {
        ObjPool* pool = ObjPool::fromMemoryRaw();
        Composer composer(pool);
        Window::createHeadless(composer);

        delete pool;

        STD_INSIST(composer.window == nullptr);
    }
}
