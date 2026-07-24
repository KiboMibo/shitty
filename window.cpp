/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "window.h"

#include "clipboard.h"
#include "composer.h"
#include "desktop_actions.h"
#include "input_sink.h"
#include "options.h"
#include "pty_event_source.h"
#include "test_mode.h"
#include "vk_renderer.h"

#include <std/alg/minmax.h>
#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/sys/throw.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cerrno>
#include <climits>
#include <cmath>
#include <cstring>
#include <exception>
#include <spawn.h>

namespace stl {}

using namespace stl;

extern char** environ;

namespace {
    struct WindowImpl final: public Window, public Clipboard, public DesktopActions, public PtyEventHost {
        explicit WindowImpl(Composer& composer);
        ~WindowImpl();

        void initialize() override;
        float density() override;
        void show() override;
        void activate() override;
        WindowEvents dispatchEvents(double timeout) override;

        void setTitle(StringView title) override;
        void requestAttention() override;
        void requestRedraw() override;
        void restore() override;
        void iconify() override;
        void move(i32 x, i32 y) override;
        void focus() override;
        void setMaximized(bool maximized) override;
        void setFullscreen(bool fullscreen) override;
        void resizePixels(u32 width, u32 height) override;
        WindowInfo info() override;

        Renderer* createRender() override;
        TestModeInput* testApi() override;

        StringView readPrimary() override;
        StringView readClipboard() override;
        void writePrimary(StringView content) override;
        void writeClipboard(StringView content) override;

        void openUri(StringView uri) override;
        void pointerIcon(PointerIcon icon) override;

        void wake() override;

        bool keyPressed(int key);
        u16 keyboardModifiers();
        u16 inputModifiers(int modifiers);
        InputKey inputKey(int key);
        u32 decodeKeyName(const char* name);
        u32 baseLayoutKey(int key);
        void keyEvent(int key, int scancode, int action, int rawModifiers);
        void textInput(u32 codepoint, int rawModifiers);
        double pixelScaleX();
        double pixelScaleY();
        int toPixelX(double x);
        int toPixelY(double y);
        void mouseButton(int button, bool pressed, int modifiers);
        void mouseMotion(double x, double y);
        void mouseWheel(double x, double y);

        int gridAlignedWindowSize(int framebufferSize, int border, int cellSize, float scale, int currentWindowSize);
        void publishResize(int width, int height);
        void onFramebufferSize(int width, int height);
        void setupCallbacks();
        static WindowImpl& fromWindow(GLFWwindow* window);

        template <typename Fn>
        void guardCallback(Fn&& callback);

        [[noreturn]]
        void fail(const char* operation);

        Composer& composer;
        GLFWwindow* window = nullptr;
        GLFWcursor* cursor = nullptr;
        GLFWcursor* hyperlinkCursor = nullptr;
        bool initialized = false;
        bool callbacksActive = false;
        bool resizePending = false;
        bool redrawPending = false;
        bool correctingResize = false;
        bool attentionRequested = false;
        int restoredX = 0;
        int restoredY = 0;
        int restoredWidth = 800;
        int restoredHeight = 600;
        std::exception_ptr callbackError;
        Buffer primarySelection;
        Buffer clipboardReadBuffer;
        Buffer clipboardWriteBuffer;
        Buffer textBuffer;
        Buffer uriBuffer;
    };

    struct TestModeInputImpl final: public TestModeInput {
        explicit TestModeInputImpl(WindowImpl* window);

        void testKeyEvent(int key, int scancode, int action, int modifiers) override;
        void testTextInput(unsigned codepoint, int modifiers) override;

        WindowImpl* window;
    };
}

WindowImpl::WindowImpl(Composer& composer_)
    : composer(composer_)
{
    composer.window = this;
    composer.clipboard = this;
    composer.desktopActions = this;
    composer.ptyEventHost = this;
}

WindowImpl::~WindowImpl() {
    if (composer.window == this) {
        composer.window = nullptr;
    }
    if (composer.clipboard == this) {
        composer.clipboard = nullptr;
    }
    if (composer.desktopActions == this) {
        composer.desktopActions = nullptr;
    }
    if (composer.ptyEventHost == this) {
        composer.ptyEventHost = nullptr;
    }
    if (cursor != nullptr) {
        glfwDestroyCursor(cursor);
    }
    if (hyperlinkCursor != nullptr) {
        glfwDestroyCursor(hyperlinkCursor);
    }
    if (window != nullptr) {
        glfwDestroyWindow(window);
    }
    if (initialized) {
        glfwTerminate();
    }
}

TestModeInputImpl::TestModeInputImpl(WindowImpl* window_)
    : window(window_)
{
}

void TestModeInputImpl::testKeyEvent(int key, int scancode, int action, int modifiers) {
    window->keyEvent(key, scancode, action, modifiers);
}

void TestModeInputImpl::testTextInput(unsigned codepoint, int modifiers) {
    window->textInput(codepoint, modifiers);
}

[[noreturn]]
void WindowImpl::fail(const char* operation) {
    const char* description = nullptr;
    const int code = glfwGetError(&description);
    if (description != nullptr) {
        Errno(EINVAL).raise(StringBuilder() << StringView(operation) << StringView(u8" failed: ") << StringView(description));
    }
    if (code != GLFW_NO_ERROR) {
        Errno(EINVAL).raise(StringBuilder() << StringView(operation) << StringView(u8" failed (GLFW error ") << (i64)(code) << StringView(u8")"));
    }
    Errno(EINVAL).raise(StringBuilder() << StringView(operation) << StringView(u8" failed"));
}

void WindowImpl::initialize() {
    if (initialized) {
        return;
    }

    if (!glfwInit()) {
        fail("glfwInit");
    }
    initialized = true;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_TRUE);
    const int initialWidth = max(320, (int)(opts.nCols) * opts.fontsize / 2);
    const int initialHeight = max(200, (int)(opts.nRows) * opts.fontsize);
    window = glfwCreateWindow(initialWidth, initialHeight, opts.title, nullptr, nullptr);
    if (window == nullptr) {
        fail("glfwCreateWindow");
    }
    glfwSetWindowUserPointer(window, this);
    glfwSetInputMode(window, GLFW_LOCK_KEY_MODS, GLFW_TRUE);
}

float WindowImpl::density() {
    float xScale = 1.0f;
    float yScale = 1.0f;
    glfwGetWindowContentScale(window, &xScale, &yScale);
    return max(1.0f, max(xScale, yScale));
}

void WindowImpl::show() {
    const float scale = density();
    const int desiredPixelWidth = 2 * opts.border + opts.nCols * composer.glyphWidth;
    const int desiredPixelHeight = 2 * opts.border + opts.nRows * composer.glyphHeight;
    const int desiredWidth = max(1, (int)(ceil(desiredPixelWidth / scale)));
    const int desiredHeight = max(1, (int)(ceil(desiredPixelHeight / scale)));
    const int minimumWidth = max(1, (int)(ceil((2 * opts.border + composer.glyphWidth) / scale)));
    const int minimumHeight = max(1, (int)(ceil((2 * opts.border + composer.glyphHeight) / scale)));

    glfwSetWindowSizeLimits(window, minimumWidth, minimumHeight, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwSetWindowSize(window, desiredWidth, desiredHeight);
    glfwShowWindow(window);
    glfwPollEvents();

    int pixelWidth = 0;
    int pixelHeight = 0;
    glfwGetFramebufferSize(window, &pixelWidth, &pixelHeight);
    if (pixelWidth <= 0 || pixelHeight <= 0 || pixelWidth > UINT16_MAX || pixelHeight > UINT16_MAX) {
        Errno(EINVAL).raise(StringBuilder() << StringView(u8"initial framebuffer has invalid size"));
    }
    composer.resize((u16)(pixelWidth), (u16)(pixelHeight));

    cursor = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    hyperlinkCursor = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
    if (cursor != nullptr) {
        glfwSetCursor(window, cursor);
    }
}

void WindowImpl::activate() {
    if (callbacksActive) {
        return;
    }
    setupCallbacks();
    callbacksActive = true;
    composer.input->focus(glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE);
}

WindowEvents WindowImpl::dispatchEvents(double timeout) {
    if (!glfwWindowShouldClose(window)) {
        if (timeout == 0.0) {
            glfwPollEvents();
        } else if (timeout > 0.0) {
            glfwWaitEventsTimeout(timeout);
        } else {
            glfwWaitEvents();
        }
    }
    if (callbackError != nullptr) {
        std::rethrow_exception(callbackError);
    }
    composer.input->flush();

    const WindowEvents result{
        .close = glfwWindowShouldClose(window) != 0,
        .resized = resizePending,
        .redraw = redrawPending,
    };
    resizePending = false;
    redrawPending = false;
    return result;
}

void WindowImpl::setTitle(StringView title) {
    textBuffer.reset();
    textBuffer.append(title.data(), title.length());
    glfwSetWindowTitle(window, textBuffer.cStr());
}

void WindowImpl::requestAttention() {
    if (glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE) {
        attentionRequested = false;
        return;
    }
    if (!attentionRequested) {
        attentionRequested = true;
        glfwRequestWindowAttention(window);
    }
}

void WindowImpl::requestRedraw() {
    redrawPending = true;
    wake();
}

void WindowImpl::restore() {
    glfwRestoreWindow(window);
}

void WindowImpl::iconify() {
    glfwIconifyWindow(window);
}

void WindowImpl::move(i32 x, i32 y) {
    glfwSetWindowPos(window, x, y);
}

void WindowImpl::focus() {
    glfwFocusWindow(window);
}

void WindowImpl::setMaximized(bool maximized) {
    if (maximized) {
        glfwMaximizeWindow(window);
    } else {
        glfwRestoreWindow(window);
    }
}

void WindowImpl::setFullscreen(bool fullscreen) {
    const bool current = glfwGetWindowMonitor(window) != nullptr;
    if (fullscreen == current) {
        return;
    }
    if (fullscreen) {
        glfwGetWindowPos(window, &restoredX, &restoredY);
        glfwGetWindowSize(window, &restoredWidth, &restoredHeight);
        GLFWmonitor* const monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* const mode = monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
        if (monitor != nullptr && mode != nullptr) {
            glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        }
        return;
    }
    glfwSetWindowMonitor(window, nullptr, restoredX, restoredY, restoredWidth, restoredHeight, GLFW_DONT_CARE);
}

void WindowImpl::resizePixels(u32 width, u32 height) {
    float xScale = 1.0f;
    float yScale = 1.0f;
    glfwGetWindowContentScale(window, &xScale, &yScale);
    glfwSetWindowSize(window, max(1, (int)(ceil(width / xScale))), max(1, (int)(ceil(height / yScale))));
}

WindowInfo WindowImpl::info() {
    WindowInfo result;
    glfwGetWindowPos(window, &result.x, &result.y);
    GLFWmonitor* monitor = glfwGetWindowMonitor(window);
    if (monitor == nullptr) {
        monitor = glfwGetPrimaryMonitor();
    }
    const GLFWvidmode* const mode = monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
    if (mode != nullptr) {
        result.screenPixelWidth = mode->width;
        result.screenPixelHeight = mode->height;
    }
    result.iconified = glfwGetWindowAttrib(window, GLFW_ICONIFIED);
    result.maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED);
    result.fullscreen = glfwGetWindowMonitor(window) != nullptr;
    return result;
}

Renderer* WindowImpl::createRender() {
    return Renderer::create(composer, window);
}

TestModeInput* WindowImpl::testApi() {
#ifdef SHITTY_FOR_TESTS
    return composer.pool->make<TestModeInputImpl>(this);
#else
    return nullptr;
#endif
}

StringView WindowImpl::readPrimary() {
    return StringView(primarySelection);
}

StringView WindowImpl::readClipboard() {
    clipboardReadBuffer.reset();
    if (window == nullptr) {
        return StringView(clipboardReadBuffer);
    }
    const char* const text = glfwGetClipboardString(window);
    if (text != nullptr) {
        clipboardReadBuffer.append(text, strlen(text));
    }
    return StringView(clipboardReadBuffer);
}

void WindowImpl::writePrimary(StringView content) {
    primarySelection.reset();
    primarySelection.append(content.data(), content.length());
}

void WindowImpl::writeClipboard(StringView content) {
    clipboardWriteBuffer.reset();
    clipboardWriteBuffer.append(content.data(), content.length());
    if (window != nullptr) {
        glfwSetClipboardString(window, clipboardWriteBuffer.cStr());
    }
}

void WindowImpl::openUri(StringView uri) {
    uriBuffer.reset();
    uriBuffer.append(uri.data(), uri.length());
    char* const path = uriBuffer.cStr();
    char* const arguments[] = {
        (char*)("xdg-open"),
        path,
        nullptr,
    };
    pid_t pid = -1;
    posix_spawnp(&pid, arguments[0], nullptr, nullptr, arguments, environ);
}

void WindowImpl::pointerIcon(PointerIcon icon) {
    if (window == nullptr) {
        return;
    }
    GLFWcursor* const selected = icon == PointerIcon::Link && hyperlinkCursor != nullptr ? hyperlinkCursor : cursor;
    glfwSetCursor(window, selected);
}

void WindowImpl::wake() {
    glfwPostEmptyEvent();
}

bool WindowImpl::keyPressed(int key) {
    return window != nullptr && glfwGetKey(window, key) == GLFW_PRESS;
}

u16 WindowImpl::keyboardModifiers() {
    u16 modifiers = 0;
    if (keyPressed(GLFW_KEY_LEFT_SHIFT) || keyPressed(GLFW_KEY_RIGHT_SHIFT)) {
        modifiers |= InputShift;
    }
    if (keyPressed(GLFW_KEY_LEFT_CONTROL) || keyPressed(GLFW_KEY_RIGHT_CONTROL)) {
        modifiers |= InputControl;
    }
    if (keyPressed(GLFW_KEY_LEFT_ALT)) {
        modifiers |= InputAlt;
    }
    if (keyPressed(GLFW_KEY_RIGHT_ALT)) {
        modifiers |= InputAltGraph;
    }
    if (keyPressed(GLFW_KEY_LEFT_SUPER) || keyPressed(GLFW_KEY_RIGHT_SUPER)) {
        modifiers |= InputSuper;
    }
    return modifiers;
}

u16 WindowImpl::inputModifiers(int modifiers) {
    u16 result = 0;
    if (modifiers & GLFW_MOD_SHIFT) {
        result |= InputShift;
    }
    if (modifiers & GLFW_MOD_CONTROL) {
        result |= InputControl;
    }
    if (modifiers & GLFW_MOD_ALT) {
        result |= keyPressed(GLFW_KEY_RIGHT_ALT) ? InputAltGraph : InputAlt;
    }
    if (modifiers & GLFW_MOD_SUPER) {
        result |= InputSuper;
    }
    if (modifiers & GLFW_MOD_CAPS_LOCK) {
        result |= InputCapsLock;
    }
    if (modifiers & GLFW_MOD_NUM_LOCK) {
        result |= InputNumLock;
    }
    return result;
}

InputKey WindowImpl::inputKey(int key) {
    switch (key) {
        case GLFW_KEY_ESCAPE:
            return InputKey::Escape;
        case GLFW_KEY_ENTER:
            return InputKey::Enter;
        case GLFW_KEY_BACKSPACE:
            return InputKey::Backspace;
        case GLFW_KEY_TAB:
            return InputKey::Tab;
        case GLFW_KEY_INSERT:
            return InputKey::Insert;
        case GLFW_KEY_DELETE:
            return InputKey::Delete;
        case GLFW_KEY_HOME:
            return InputKey::Home;
        case GLFW_KEY_END:
            return InputKey::End;
        case GLFW_KEY_UP:
            return InputKey::Up;
        case GLFW_KEY_DOWN:
            return InputKey::Down;
        case GLFW_KEY_LEFT:
            return InputKey::Left;
        case GLFW_KEY_RIGHT:
            return InputKey::Right;
        case GLFW_KEY_PAGE_UP:
            return InputKey::PageUp;
        case GLFW_KEY_PAGE_DOWN:
            return InputKey::PageDown;
        case GLFW_KEY_F1:
            return InputKey::F1;
        case GLFW_KEY_F2:
            return InputKey::F2;
        case GLFW_KEY_F3:
            return InputKey::F3;
        case GLFW_KEY_F4:
            return InputKey::F4;
        case GLFW_KEY_F5:
            return InputKey::F5;
        case GLFW_KEY_F6:
            return InputKey::F6;
        case GLFW_KEY_F7:
            return InputKey::F7;
        case GLFW_KEY_F8:
            return InputKey::F8;
        case GLFW_KEY_F9:
            return InputKey::F9;
        case GLFW_KEY_F10:
            return InputKey::F10;
        case GLFW_KEY_F11:
            return InputKey::F11;
        case GLFW_KEY_F12:
            return InputKey::F12;
        case GLFW_KEY_F13:
            return InputKey::F13;
        case GLFW_KEY_F14:
            return InputKey::F14;
        case GLFW_KEY_F15:
            return InputKey::F15;
        case GLFW_KEY_F16:
            return InputKey::F16;
        case GLFW_KEY_F17:
            return InputKey::F17;
        case GLFW_KEY_F18:
            return InputKey::F18;
        case GLFW_KEY_F19:
            return InputKey::F19;
        case GLFW_KEY_F20:
            return InputKey::F20;
        case GLFW_KEY_KP_0:
            return InputKey::Keypad0;
        case GLFW_KEY_KP_1:
            return InputKey::Keypad1;
        case GLFW_KEY_KP_2:
            return InputKey::Keypad2;
        case GLFW_KEY_KP_3:
            return InputKey::Keypad3;
        case GLFW_KEY_KP_4:
            return InputKey::Keypad4;
        case GLFW_KEY_KP_5:
            return InputKey::Keypad5;
        case GLFW_KEY_KP_6:
            return InputKey::Keypad6;
        case GLFW_KEY_KP_7:
            return InputKey::Keypad7;
        case GLFW_KEY_KP_8:
            return InputKey::Keypad8;
        case GLFW_KEY_KP_9:
            return InputKey::Keypad9;
        case GLFW_KEY_KP_DECIMAL:
            return InputKey::KeypadDecimal;
        case GLFW_KEY_KP_DIVIDE:
            return InputKey::KeypadDivide;
        case GLFW_KEY_KP_MULTIPLY:
            return InputKey::KeypadMultiply;
        case GLFW_KEY_KP_SUBTRACT:
            return InputKey::KeypadSubtract;
        case GLFW_KEY_KP_ADD:
            return InputKey::KeypadAdd;
        case GLFW_KEY_KP_ENTER:
            return InputKey::KeypadEnter;
        case GLFW_KEY_KP_EQUAL:
            return InputKey::KeypadEqual;
        case GLFW_KEY_CAPS_LOCK:
            return InputKey::CapsLock;
        case GLFW_KEY_SCROLL_LOCK:
            return InputKey::ScrollLock;
        case GLFW_KEY_NUM_LOCK:
            return InputKey::NumLock;
        case GLFW_KEY_PRINT_SCREEN:
            return InputKey::PrintScreen;
        case GLFW_KEY_PAUSE:
            return InputKey::Pause;
        case GLFW_KEY_MENU:
            return InputKey::Menu;
        case GLFW_KEY_LEFT_SHIFT:
            return InputKey::LeftShift;
        case GLFW_KEY_LEFT_CONTROL:
            return InputKey::LeftControl;
        case GLFW_KEY_LEFT_ALT:
            return InputKey::LeftAlt;
        case GLFW_KEY_LEFT_SUPER:
            return InputKey::LeftSuper;
        case GLFW_KEY_RIGHT_SHIFT:
            return InputKey::RightShift;
        case GLFW_KEY_RIGHT_CONTROL:
            return InputKey::RightControl;
        case GLFW_KEY_RIGHT_ALT:
            return InputKey::RightAlt;
        case GLFW_KEY_RIGHT_SUPER:
            return InputKey::RightSuper;
        default:
            return baseLayoutKey(key) != 0 ? InputKey::Printable : InputKey::Unknown;
    }
}

u32 WindowImpl::decodeKeyName(const char* name) {
    if (name == nullptr || !*name) {
        return 0;
    }
    const auto* bytes = (const unsigned char*)(name);
    if (bytes[0] < 0x80) {
        return bytes[0];
    }
    if ((bytes[0] & 0xe0) == 0xc0 && bytes[1]) {
        return ((bytes[0] & 0x1f) << 6) | (bytes[1] & 0x3f);
    }
    if ((bytes[0] & 0xf0) == 0xe0 && bytes[1] && bytes[2]) {
        return ((bytes[0] & 0x0f) << 12) | ((bytes[1] & 0x3f) << 6) | (bytes[2] & 0x3f);
    }
    if ((bytes[0] & 0xf8) == 0xf0 && bytes[1] && bytes[2] && bytes[3]) {
        return ((bytes[0] & 0x07) << 18) | ((bytes[1] & 0x3f) << 12) | ((bytes[2] & 0x3f) << 6) | (bytes[3] & 0x3f);
    }
    return 0;
}

u32 WindowImpl::baseLayoutKey(int key) {
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
        return key - GLFW_KEY_A + 'a';
    }
    if ((key >= GLFW_KEY_0 && key <= GLFW_KEY_9) || key == GLFW_KEY_SPACE || key == GLFW_KEY_APOSTROPHE || key == GLFW_KEY_COMMA || key == GLFW_KEY_MINUS || key == GLFW_KEY_PERIOD || key == GLFW_KEY_SLASH || key == GLFW_KEY_SEMICOLON || key == GLFW_KEY_EQUAL || key == GLFW_KEY_LEFT_BRACKET || key == GLFW_KEY_BACKSLASH || key == GLFW_KEY_RIGHT_BRACKET || key == GLFW_KEY_GRAVE_ACCENT) {
        return key;
    }
    return 0;
}

void WindowImpl::keyEvent(int key, int scancode, int action, int rawModifiers) {
    InputAction inputAction;
    switch (action) {
        case GLFW_PRESS:
            inputAction = InputAction::Press;
            break;
        case GLFW_REPEAT:
            inputAction = InputAction::Repeat;
            break;
        case GLFW_RELEASE:
            inputAction = InputAction::Release;
            break;
        default:
            return;
    }
    const InputKey translated = inputKey(key);
    if (translated == InputKey::Unknown) {
        return;
    }
    KeyInput input;
    input.key = translated;
    input.action = inputAction;
    input.modifiers = inputModifiers(rawModifiers);
    if (translated == InputKey::RightAlt) {
        input.modifiers = (input.modifiers & ~InputAlt) | InputAltGraph;
    }
    input.layoutCodepoint = decodeKeyName(glfwGetKeyName(key, scancode));
    input.baseCodepoint = baseLayoutKey(key);
    composer.input->key(input);
}

void WindowImpl::textInput(u32 codepoint, int rawModifiers) {
    if (codepoint != 0) {
        composer.input->text({codepoint, inputModifiers(rawModifiers)});
    }
}

double WindowImpl::pixelScaleX() {
    int windowWidth = 0;
    int framebufferWidth = 0;
    glfwGetWindowSize(window, &windowWidth, nullptr);
    glfwGetFramebufferSize(window, &framebufferWidth, nullptr);
    return windowWidth > 0 ? max(1.0, (double)(framebufferWidth) / windowWidth) : 1.0;
}

double WindowImpl::pixelScaleY() {
    int windowHeight = 0;
    int framebufferHeight = 0;
    glfwGetWindowSize(window, nullptr, &windowHeight);
    glfwGetFramebufferSize(window, nullptr, &framebufferHeight);
    return windowHeight > 0 ? max(1.0, (double)(framebufferHeight) / windowHeight) : 1.0;
}

int WindowImpl::toPixelX(double x) {
    if (!isfinite(x)) {
        return 0;
    }
    return (int)(max((double)(INT_MIN), min((double)(INT_MAX), round(x * pixelScaleX()))));
}

int WindowImpl::toPixelY(double y) {
    if (!isfinite(y)) {
        return 0;
    }
    return (int)(max((double)(INT_MIN), min((double)(INT_MAX), round(y * pixelScaleY()))));
}

void WindowImpl::mouseButton(int button, bool pressed, int modifiers) {
    if (button < GLFW_MOUSE_BUTTON_1 || button > GLFW_MOUSE_BUTTON_8) {
        return;
    }
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    PointerButtonInput input;
    input.button = (PointerButton)(button);
    input.pressed = pressed;
    input.pixelX = toPixelX(x);
    input.pixelY = toPixelY(y);
    input.modifiers = inputModifiers(modifiers);
    input.time = glfwGetTime();
    composer.input->pointerButton(input);
}

void WindowImpl::mouseMotion(double x, double y) {
    composer.input->pointerMotion({toPixelX(x), toPixelY(y), keyboardModifiers()});
}

void WindowImpl::mouseWheel(double x, double y) {
    double pointerX = 0.0;
    double pointerY = 0.0;
    glfwGetCursorPos(window, &pointerX, &pointerY);
    composer.input->scroll({x, y, toPixelX(pointerX), toPixelY(pointerY), keyboardModifiers()});
}

int WindowImpl::gridAlignedWindowSize(int framebufferSize, int border, int cellSize, float scale, int currentWindowSize) {
    const int innerSize = framebufferSize - 2 * border;
    if (innerSize < cellSize || scale <= 0.0f) {
        return currentWindowSize;
    }
    const int scaleNumerator = max(120, (int)(lround(scale * 120.0f)));
    for (int cells = innerSize / cellSize; cells > 0; --cells) {
        const int framebufferTarget = 2 * border + cells * cellSize;
        const int windowTarget = (int)(((i64)(framebufferTarget) * 120 + scaleNumerator - 1) / scaleNumerator);
        if ((i64)(windowTarget)*scaleNumerator / 120 == framebufferTarget) {
            return windowTarget;
        }
    }
    return currentWindowSize;
}

void WindowImpl::publishResize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    composer.resize((u16)(min(width, (int)(UINT16_MAX))), (u16)(min(height, (int)(UINT16_MAX))));
    resizePending = true;
}

void WindowImpl::onFramebufferSize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    if (correctingResize || glfwGetWindowMonitor(window) != nullptr || glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE) {
        publishResize(width, height);
        return;
    }

    int windowWidth = 0;
    int windowHeight = 0;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
    float xScale = 1.0f;
    float yScale = 1.0f;
    glfwGetWindowContentScale(window, &xScale, &yScale);
    const int snappedWidth = gridAlignedWindowSize(width, opts.border, composer.glyphWidth, xScale, windowWidth);
    const int snappedHeight = gridAlignedWindowSize(height, opts.border, composer.glyphHeight, yScale, windowHeight);
    if (snappedWidth == windowWidth && snappedHeight == windowHeight) {
        publishResize(width, height);
        return;
    }

    correctingResize = true;
    glfwSetWindowSize(window, snappedWidth, snappedHeight);
    correctingResize = false;
    glfwGetFramebufferSize(window, &width, &height);
    publishResize(width, height);
}

template <typename Fn>
void WindowImpl::guardCallback(Fn&& callback) {
    if (callbackError != nullptr) {
        return;
    }
    try {
        callback();
    } catch (...) {
        callbackError = std::current_exception();
    }
}

void WindowImpl::setupCallbacks() {
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* source, int width, int height) {
        fromWindow(source).guardCallback([&]() {
            fromWindow(source).onFramebufferSize(width, height);
        });
    });
    glfwSetWindowRefreshCallback(window, [](GLFWwindow* source) {
        fromWindow(source).redrawPending = true;
    });
    glfwSetWindowFocusCallback(window, [](GLFWwindow* source, int focused) {
        fromWindow(source).guardCallback([&]() {
            WindowImpl& self = fromWindow(source);
            if (focused) {
                self.attentionRequested = false;
            }
            self.composer.input->focus(focused == GLFW_TRUE);
        });
    });
    glfwSetKeyCallback(window, [](GLFWwindow* source, int key, int scancode, int action, int modifiers) {
        fromWindow(source).guardCallback([&]() {
            fromWindow(source).keyEvent(key, scancode, action, modifiers);
        });
    });
    glfwSetCharCallback(window, [](GLFWwindow* source, unsigned codepoint) {
        fromWindow(source).guardCallback([&]() {
            WindowImpl& self = fromWindow(source);
            self.composer.input->text({codepoint, self.keyboardModifiers()});
        });
    });
    glfwSetMouseButtonCallback(window, [](GLFWwindow* source, int button, int action, int modifiers) {
        fromWindow(source).guardCallback([&]() {
            fromWindow(source).mouseButton(button, action == GLFW_PRESS, modifiers);
        });
    });
    glfwSetCursorPosCallback(window, [](GLFWwindow* source, double x, double y) {
        fromWindow(source).guardCallback([&]() {
            fromWindow(source).mouseMotion(x, y);
        });
    });
    glfwSetCursorEnterCallback(window, [](GLFWwindow* source, int entered) {
        fromWindow(source).guardCallback([&]() {
            fromWindow(source).composer.input->pointerPresence(entered == GLFW_TRUE);
        });
    });
    glfwSetScrollCallback(window, [](GLFWwindow* source, double x, double y) {
        fromWindow(source).guardCallback([&]() {
            fromWindow(source).mouseWheel(x, y);
        });
    });
}

WindowImpl& WindowImpl::fromWindow(GLFWwindow* window) {
    return *(WindowImpl*)(glfwGetWindowUserPointer(window));
}

Window* Window::create(Composer& composer) {
    return composer.pool->make<WindowImpl>(composer);
}
