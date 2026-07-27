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
#include "listener.h"
#include "options.h"
#include "poller.h"
#include "test_mode.h"
#include "vk_renderer.h"

#include <std/alg/minmax.h>
#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/sys/throw.h>

#define GLFW_INCLUDE_NONE
#include "third_party/glfw/include/GLFW/glfw3.h"

#include <cerrno>
#include <climits>
#include <cmath>
#include <cstring>
#include <exception>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace stl;

extern char** environ;

namespace {
    struct InputTranslator {
        static u16 modifiers(int modifiers, bool rightAltPressed);
        static InputKey key(int key);
        static u32 decodeKeyName(const char* name);
        static u32 baseLayoutKey(int key);
        static void sendKeyEvent(Composer& composer, int key, int action, int rawModifiers, bool rightAltPressed, u32 layoutCodepoint);
        static void sendTextInput(Composer& composer, u32 codepoint, int rawModifiers);
        static void updateContentScale(Composer& composer, float xScale, float yScale);
    };

    struct GlfwWindowImpl final: public Window, public Clipboard, public DesktopActions {
        explicit GlfwWindowImpl(Composer& composer);
        ~GlfwWindowImpl();

        void initialize() override;
        void show() override;
        void activate() override;
        void requestClose() override;
        bool dispatchEvents() override;

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

        bool handlesUriScheme(StringView scheme) override;
        void openUri(StringView uri) override;
        void pointerIcon(PointerIcon icon) override;

        bool keyPressed(int key);
        u16 keyboardModifiers();
        u16 inputModifiers(int modifiers);
        void keyEvent(int key, int scancode, int action, int rawModifiers);
        void refreshContentScale();
        void contentScale(float xScale, float yScale);
        double pixelScaleX();
        double pixelScaleY();
        int toPixelX(double x);
        int toPixelY(double y);
        void mouseButton(int button, bool pressed, int modifiers);
        void mouseMotion(double x, double y);
        void mouseWheel(double x, double y);

        int gridAlignedWindowSize(int framebufferSize, int border, int cellSize, float scale, int currentWindowSize);
        void configureGridSize();
        void queueResize(int width, int height);
        void onFramebufferSize(int width, int height);
        void setupCallbacks();
        static int pollCallback(struct pollfd* fds, size_t count, double* timeout, void* user);
        bool queryUriScheme(StringView scheme);
        static GlfwWindowImpl& fromWindow(GLFWwindow* window);

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
        // A dispatch can carry several configures; terminal reflow consumes only the last one after GLFW returns.
        u16 pendingPixelWidth = 0;
        u16 pendingPixelHeight = 0;
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

        struct UriScheme {
            StringView name;
            bool handled = false;
        };

        static constexpr size_t uriSchemeCapacity = 64;
        UriScheme uriSchemes[uriSchemeCapacity];
        size_t uriSchemeCount = 0;
    };

    struct HeadlessWindowImpl final: public Window, public TestModeInput {
        explicit HeadlessWindowImpl(Composer& composer);
        ~HeadlessWindowImpl();

        void initialize() override;
        void show() override;
        void activate() override;
        void requestClose() override;
        bool dispatchEvents() override;

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

        void testKeyEvent(int key, int scancode, int action, int modifiers) override;
        void testTextInput(unsigned codepoint, int modifiers) override;
        void testContentScale(float xScale, float yScale) override;

        Composer& composer;
    };
}

GlfwWindowImpl::GlfwWindowImpl(Composer& composer_)
    : composer(composer_)
{
    composer.window = this;
    composer.clipboard = this;
    composer.desktopActions = this;
}

GlfwWindowImpl::~GlfwWindowImpl() {
    composer.window = nullptr;
    composer.clipboard = nullptr;
    composer.desktopActions = nullptr;
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
        glfwSetPollCallback(nullptr, nullptr);
        glfwTerminate();
    }
}

HeadlessWindowImpl::HeadlessWindowImpl(Composer& composer_)
    : composer(composer_)
{
    composer.window = this;
}

HeadlessWindowImpl::~HeadlessWindowImpl() {
    composer.window = nullptr;
}

void HeadlessWindowImpl::initialize() {
}

void HeadlessWindowImpl::show() {
}

void HeadlessWindowImpl::activate() {
}

void HeadlessWindowImpl::requestClose() {
}

bool HeadlessWindowImpl::dispatchEvents() {
    return false;
}

void HeadlessWindowImpl::setTitle(StringView) {
}

void HeadlessWindowImpl::requestAttention() {
}

void HeadlessWindowImpl::requestRedraw() {
}

void HeadlessWindowImpl::restore() {
}

void HeadlessWindowImpl::iconify() {
}

void HeadlessWindowImpl::move(i32, i32) {
}

void HeadlessWindowImpl::focus() {
}

void HeadlessWindowImpl::setMaximized(bool) {
}

void HeadlessWindowImpl::setFullscreen(bool) {
}

void HeadlessWindowImpl::resizePixels(u32 width, u32 height) {
    composer.resize((u16)(min(width, (u32)(UINT16_MAX))), (u16)(min(height, (u32)(UINT16_MAX))));
}

WindowInfo HeadlessWindowImpl::info() {
    return {
        .screenPixelWidth = composer.pixelWidth,
        .screenPixelHeight = composer.pixelHeight,
    };
}

Renderer* HeadlessWindowImpl::createRender() {
    Errno(ENOTSUP).raise(StringView(u8"headless window has no renderer"));
}

TestModeInput* HeadlessWindowImpl::testApi() {
    return this;
}

void HeadlessWindowImpl::testKeyEvent(int key, int, int action, int modifiers) {
    InputTranslator::sendKeyEvent(composer, key, action, modifiers, false, InputTranslator::baseLayoutKey(key));
}

void HeadlessWindowImpl::testTextInput(unsigned codepoint, int modifiers) {
    InputTranslator::sendTextInput(composer, codepoint, modifiers);
}

void HeadlessWindowImpl::testContentScale(float xScale, float yScale) {
    InputTranslator::updateContentScale(composer, xScale, yScale);
}

[[noreturn]]
void GlfwWindowImpl::fail(const char* operation) {
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

void GlfwWindowImpl::initialize() {
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
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, "shitty");
    glfwWindowHintString(GLFW_X11_CLASS_NAME, "shitty");
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "shitty");
    const int initialWidth = max(320, (int)(opts.nCols) * opts.fontsize / 2);
    const int initialHeight = max(200, (int)(opts.nRows) * opts.fontsize);
    window = glfwCreateWindow(initialWidth, initialHeight, opts.title, nullptr, nullptr);
    if (window == nullptr) {
        fail("glfwCreateWindow");
    }
    glfwSetWindowUserPointer(window, this);
    glfwSetInputMode(window, GLFW_LOCK_KEY_MODS, GLFW_TRUE);
    glfwSetWindowContentScaleCallback(window, [](GLFWwindow* source, float xScale, float yScale) {
        fromWindow(source).guardCallback([&]() {
            fromWindow(source).contentScale(xScale, yScale);
        });
    });
    refreshContentScale();
}

void GlfwWindowImpl::refreshContentScale() {
    float xScale = 1.0f;
    float yScale = 1.0f;
    glfwGetWindowContentScale(window, &xScale, &yScale);
    contentScale(xScale, yScale);
}

void GlfwWindowImpl::configureGridSize() {
    const float scale = composer.contentScale;
    const int desiredPixelWidth = 2 * opts.border + opts.nCols * composer.glyphWidth;
    const int desiredPixelHeight = 2 * opts.border + opts.nRows * composer.glyphHeight;
    const int desiredWidth = max(1, (int)(ceil(desiredPixelWidth / scale)));
    const int desiredHeight = max(1, (int)(ceil(desiredPixelHeight / scale)));
    const int minimumWidth = max(1, (int)(ceil((2 * opts.border + composer.glyphWidth) / scale)));
    const int minimumHeight = max(1, (int)(ceil((2 * opts.border + composer.glyphHeight) / scale)));

    glfwSetWindowSizeLimits(window, minimumWidth, minimumHeight, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwSetWindowSize(window, desiredWidth, desiredHeight);
}

void GlfwWindowImpl::show() {
    configureGridSize();
    glfwShowWindow(window);
    glfwPollEvents();
    refreshContentScale();
    configureGridSize();
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
    glfwSetCursor(window, cursor);
}

void GlfwWindowImpl::activate() {
    if (callbacksActive) {
        return;
    }
    glfwSetPollCallback(pollCallback, this);
    setupCallbacks();
    callbacksActive = true;
    refreshContentScale();
    composer.input->focus(glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE);
}

int GlfwWindowImpl::pollCallback(struct pollfd* fds, size_t count, double* timeout, void* user) {
    GlfwWindowImpl* const self = (GlfwWindowImpl*)(user);
    return self->composer.poller->poll(fds, count, timeout);
}

void GlfwWindowImpl::requestClose() {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
    glfwPostEmptyEvent();
}

bool GlfwWindowImpl::dispatchEvents() {
    if (!glfwWindowShouldClose(window)) {
        glfwWaitEvents();
    }
    if (callbackError != nullptr) {
        std::rethrow_exception(callbackError);
    }
    composer.poller->dispatch();
    composer.input->flush();

    const bool resized = resizePending;
    if (resized) {
        resizePending = false;
        redrawPending = false;
        composer.resize(pendingPixelWidth, pendingPixelHeight);
    }
    const WindowEvents result{
        .close = glfwWindowShouldClose(window) != 0,
        .resized = resized,
        .redraw = redrawPending,
    };
    redrawPending = false;
    for (IntrusiveNode* node = composer.windowEventListeners.mutFront(); node != composer.windowEventListeners.mutEnd();) {
        Listener* const listener = static_cast<Listener*>(node);
        node = node->next;
        listener->onListen((void*)(&result));
    }
    return !result.close;
}

void GlfwWindowImpl::setTitle(StringView title) {
    textBuffer.reset();
    textBuffer.append(title.data(), title.length());
    glfwSetWindowTitle(window, textBuffer.cStr());
}

void GlfwWindowImpl::requestAttention() {
    if (glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE) {
        attentionRequested = false;
        return;
    }
    if (!attentionRequested) {
        attentionRequested = true;
        glfwRequestWindowAttention(window);
    }
}

void GlfwWindowImpl::requestRedraw() {
    redrawPending = true;
    glfwPostEmptyEvent();
}

void GlfwWindowImpl::restore() {
    glfwRestoreWindow(window);
}

void GlfwWindowImpl::iconify() {
    glfwIconifyWindow(window);
}

void GlfwWindowImpl::move(i32 x, i32 y) {
    glfwSetWindowPos(window, x, y);
}

void GlfwWindowImpl::focus() {
    glfwFocusWindow(window);
}

void GlfwWindowImpl::setMaximized(bool maximized) {
    if (maximized) {
        glfwMaximizeWindow(window);
    } else {
        glfwRestoreWindow(window);
    }
}

void GlfwWindowImpl::setFullscreen(bool fullscreen) {
    const bool current = glfwGetWindowMonitor(window) != nullptr;
    if (fullscreen == current) {
        return;
    }
    if (fullscreen) {
        glfwGetWindowPos(window, &restoredX, &restoredY);
        glfwGetWindowSize(window, &restoredWidth, &restoredHeight);
        GLFWmonitor* const monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* const mode = monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
        if (mode != nullptr) {
            glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        }
        return;
    }
    glfwSetWindowMonitor(window, nullptr, restoredX, restoredY, restoredWidth, restoredHeight, GLFW_DONT_CARE);
}

void GlfwWindowImpl::resizePixels(u32 width, u32 height) {
    float xScale = 1.0f;
    float yScale = 1.0f;
    glfwGetWindowContentScale(window, &xScale, &yScale);
    glfwSetWindowSize(window, max(1, (int)(ceil(width / xScale))), max(1, (int)(ceil(height / yScale))));
}

WindowInfo GlfwWindowImpl::info() {
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

Renderer* GlfwWindowImpl::createRender() {
    return Renderer::create(composer, window);
}

TestModeInput* GlfwWindowImpl::testApi() {
    return nullptr;
}

StringView GlfwWindowImpl::readPrimary() {
    return StringView(primarySelection);
}

StringView GlfwWindowImpl::readClipboard() {
    clipboardReadBuffer.reset();
    const char* const text = glfwGetClipboardString(window);
    if (text != nullptr) {
        clipboardReadBuffer.append(text, strlen(text));
    }
    return StringView(clipboardReadBuffer);
}

void GlfwWindowImpl::writePrimary(StringView content) {
    primarySelection.reset();
    primarySelection.append(content.data(), content.length());
}

void GlfwWindowImpl::writeClipboard(StringView content) {
    clipboardWriteBuffer.reset();
    clipboardWriteBuffer.append(content.data(), content.length());
    glfwSetClipboardString(window, clipboardWriteBuffer.cStr());
}

bool GlfwWindowImpl::queryUriScheme(StringView scheme) {
    Buffer mime;
    mime.append("x-scheme-handler/", 17);
    mime.append(scheme.data(), scheme.length());

    int output[2];
    if (pipe(output) != 0) {
        return false;
    }
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(output[0]);
        close(output[1]);
        return false;
    }
    posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, output[0]);
    posix_spawn_file_actions_addclose(&actions, output[1]);

    char* const arguments[] = {
        (char*)("xdg-mime"),
        (char*)("query"),
        (char*)("default"),
        mime.cStr(),
        nullptr,
    };
    pid_t pid = -1;
    const int spawned = posix_spawnp(&pid, arguments[0], &actions, nullptr, arguments, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(output[1]);
    if (spawned != 0) {
        close(output[0]);
        return false;
    }

    bool content = false;
    u8 bytes[256];
    while (true) {
        const ssize_t count = read(output[0], bytes, sizeof(bytes));
        if (count > 0) {
            for (ssize_t index = 0; index < count; ++index) {
                content |= bytes[index] > ' ';
            }
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    close(output[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return false;
        }
    }
    return content && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool GlfwWindowImpl::handlesUriScheme(StringView scheme) {
    for (size_t index = 0; index < uriSchemeCount; ++index) {
        const UriScheme& cached = uriSchemes[index];
        if (cached.name == scheme) {
            return cached.handled;
        }
    }
    if (uriSchemeCount == uriSchemeCapacity) {
        return false;
    }
    const bool handled = queryUriScheme(scheme);
    uriSchemes[uriSchemeCount++] = {
        .name = composer.pool->intern(scheme),
        .handled = handled,
    };
    return handled;
}

void GlfwWindowImpl::openUri(StringView uri) {
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

void GlfwWindowImpl::pointerIcon(PointerIcon icon) {
    GLFWcursor* const selected = icon == PointerIcon::Link && hyperlinkCursor != nullptr ? hyperlinkCursor : cursor;
    glfwSetCursor(window, selected);
}

bool GlfwWindowImpl::keyPressed(int key) {
    return glfwGetKey(window, key) == GLFW_PRESS;
}

u16 GlfwWindowImpl::keyboardModifiers() {
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

u16 GlfwWindowImpl::inputModifiers(int modifiers) {
    return InputTranslator::modifiers(modifiers, keyPressed(GLFW_KEY_RIGHT_ALT));
}

u16 InputTranslator::modifiers(int modifiers, bool rightAltPressed) {
    u16 result = 0;
    if (modifiers & GLFW_MOD_SHIFT) {
        result |= InputShift;
    }
    if (modifiers & GLFW_MOD_CONTROL) {
        result |= InputControl;
    }
    if (modifiers & GLFW_MOD_ALT) {
        result |= rightAltPressed ? InputAltGraph : InputAlt;
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

InputKey InputTranslator::key(int key) {
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

u32 InputTranslator::decodeKeyName(const char* name) {
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

u32 InputTranslator::baseLayoutKey(int key) {
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
        return key - GLFW_KEY_A + 'a';
    }
    if ((key >= GLFW_KEY_0 && key <= GLFW_KEY_9) || key == GLFW_KEY_SPACE || key == GLFW_KEY_APOSTROPHE || key == GLFW_KEY_COMMA || key == GLFW_KEY_MINUS || key == GLFW_KEY_PERIOD || key == GLFW_KEY_SLASH || key == GLFW_KEY_SEMICOLON || key == GLFW_KEY_EQUAL || key == GLFW_KEY_LEFT_BRACKET || key == GLFW_KEY_BACKSLASH || key == GLFW_KEY_RIGHT_BRACKET || key == GLFW_KEY_GRAVE_ACCENT) {
        return key;
    }
    return 0;
}

void GlfwWindowImpl::keyEvent(int key, int scancode, int action, int rawModifiers) {
    InputTranslator::sendKeyEvent(composer, key, action, rawModifiers, keyPressed(GLFW_KEY_RIGHT_ALT), InputTranslator::decodeKeyName(glfwGetKeyName(key, scancode)));
}

void InputTranslator::sendKeyEvent(Composer& composer, int key, int action, int rawModifiers, bool rightAltPressed, u32 layoutCodepoint) {
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
    const InputKey translated = InputTranslator::key(key);
    if (translated == InputKey::Unknown) {
        return;
    }
    KeyInput input;
    input.key = translated;
    input.action = inputAction;
    input.modifiers = modifiers(rawModifiers, rightAltPressed);
    if (translated == InputKey::RightAlt) {
        input.modifiers = (input.modifiers & ~InputAlt) | InputAltGraph;
    }
    input.layoutCodepoint = layoutCodepoint;
    input.baseCodepoint = baseLayoutKey(key);
    composer.input->key(input);
}

void InputTranslator::sendTextInput(Composer& composer, u32 codepoint, int rawModifiers) {
    if (codepoint != 0) {
        composer.input->text({codepoint, modifiers(rawModifiers, false)});
    }
}

void GlfwWindowImpl::contentScale(float xScale, float yScale) {
    InputTranslator::updateContentScale(composer, xScale, yScale);
}

void InputTranslator::updateContentScale(Composer& composer, float xScale, float yScale) {
    const float scale = max(xScale, yScale);
    if (isfinite(scale) && scale > 0.0f) {
        composer.setContentScale(scale);
    }
}

double GlfwWindowImpl::pixelScaleX() {
    int windowWidth = 0;
    int framebufferWidth = 0;
    glfwGetWindowSize(window, &windowWidth, nullptr);
    glfwGetFramebufferSize(window, &framebufferWidth, nullptr);
    return windowWidth > 0 ? max(1.0, (double)(framebufferWidth) / windowWidth) : 1.0;
}

double GlfwWindowImpl::pixelScaleY() {
    int windowHeight = 0;
    int framebufferHeight = 0;
    glfwGetWindowSize(window, nullptr, &windowHeight);
    glfwGetFramebufferSize(window, nullptr, &framebufferHeight);
    return windowHeight > 0 ? max(1.0, (double)(framebufferHeight) / windowHeight) : 1.0;
}

int GlfwWindowImpl::toPixelX(double x) {
    if (!isfinite(x)) {
        return 0;
    }
    return (int)(max((double)(INT_MIN), min((double)(INT_MAX), round(x * pixelScaleX()))));
}

int GlfwWindowImpl::toPixelY(double y) {
    if (!isfinite(y)) {
        return 0;
    }
    return (int)(max((double)(INT_MIN), min((double)(INT_MAX), round(y * pixelScaleY()))));
}

void GlfwWindowImpl::mouseButton(int button, bool pressed, int modifiers) {
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

void GlfwWindowImpl::mouseMotion(double x, double y) {
    composer.input->pointerMotion({toPixelX(x), toPixelY(y), keyboardModifiers()});
}

void GlfwWindowImpl::mouseWheel(double x, double y) {
    double pointerX = 0.0;
    double pointerY = 0.0;
    glfwGetCursorPos(window, &pointerX, &pointerY);
    composer.input->scroll({x, y, toPixelX(pointerX), toPixelY(pointerY), keyboardModifiers()});
}

int GlfwWindowImpl::gridAlignedWindowSize(int framebufferSize, int border, int cellSize, float scale, int currentWindowSize) {
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

void GlfwWindowImpl::queueResize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    pendingPixelWidth = (u16)(min(width, (int)(UINT16_MAX)));
    pendingPixelHeight = (u16)(min(height, (int)(UINT16_MAX)));
    resizePending = true;
}

void GlfwWindowImpl::onFramebufferSize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    if (correctingResize) {
        queueResize(width, height);
        return;
    }
    if (glfwGetWindowMonitor(window) != nullptr || glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE || glfwGetWindowAttrib(window, GLFW_TILED) == GLFW_TRUE) {
        queueResize(width, height);
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
        queueResize(width, height);
        return;
    }

    correctingResize = true;
    glfwSetWindowSize(window, snappedWidth, snappedHeight);
    correctingResize = false;
    glfwGetFramebufferSize(window, &width, &height);
    queueResize(width, height);
}

template <typename Fn>
void GlfwWindowImpl::guardCallback(Fn&& callback) {
    if (callbackError != nullptr) {
        return;
    }
    try {
        callback();
    } catch (...) {
        callbackError = std::current_exception();
    }
}

void GlfwWindowImpl::setupCallbacks() {
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
            GlfwWindowImpl& self = fromWindow(source);
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
            GlfwWindowImpl& self = fromWindow(source);
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

GlfwWindowImpl& GlfwWindowImpl::fromWindow(GLFWwindow* window) {
    return *(GlfwWindowImpl*)(glfwGetWindowUserPointer(window));
}

Window* Window::create(Composer& composer) {
    return composer.pool->make<GlfwWindowImpl>(composer);
}

Window* Window::createHeadless(Composer& composer) {
    return composer.pool->make<HeadlessWindowImpl>(composer);
}
