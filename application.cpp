/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

/* part of this file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "application.h"
#include "clipboard.h"
#include "composer.h"
#include "desktop_actions.h"
#include "fd_redirect.h"
#include "font_pack.h"
#include "input_sink.h"
#include "osc_protocol.h"
#include "options.h"
#include "pty.h"
#include "pty_event_source.h"
#include "vk_renderer.h"
#include "startup.h"
#include "test_mode.h"
#include "vterm.h"
#include "vterm_host.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <std/ios/sys.h>
#include <std/lib/buffer.h>
#include <std/str/view.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <langinfo.h>
#include <limits.h>
#include <memory>
#include <optional>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <spawn.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include <std/mem/obj_pool.h>

namespace stl {}

using namespace stl;

extern char** environ;

namespace {
    struct ApplicationImpl final: public Application, public VtermHost, public PtyEventHost, public Clipboard, public DesktopActions {
        explicit ApplicationImpl(Composer& composer);
        ~ApplicationImpl();

        int run(int argc, char* argv[]) override;
        void osc(int command, const std::string& argument) override;
        bool handlesOsc() const override;
        void bell() override;
        bool handlesPrinter() const override;
        void print(const std::string& output) override;
        void leds(u8) override;
        void notify(const std::string&, const std::string& title, const std::string& body, bool close) override;
        void progress(u32 state, u32) override;
        void windowOperation(u32 operation, u32 first, u32 second) override;
        VtermWindowInfo windowInfo() override;
        void wake() override;
        StringView readPrimary() override;
        StringView readClipboard() override;
        void writePrimary(StringView content) override;
        void writeClipboard(StringView content) override;
        void openUri(StringView uri) override;
        void pointerIcon(PointerIcon icon) override;

        Composer& composer;
        Fontpack* fontpk = nullptr;
        Renderer* renderer = nullptr;
        Vterm* vt = nullptr;
        Pty* terminalPty = nullptr;
        GLFWwindow* window = nullptr;
        GLFWcursor* cursor = nullptr;
        GLFWcursor* hyperlinkCursor = nullptr;
        bool glfwInitialized = false;
        FILE* printerPipe = nullptr;
        bool refreshAllowed = true;
        bool refreshPending = false;
        bool committedRepaintPending = false;
        bool terminalHostReady = false;
        bool attentionRequested = false;
        std::optional<std::chrono::steady_clock::time_point> refreshDeadline;
        Buffer primarySelection;
        Buffer clipboardReadBuffer;
        Buffer clipboardWriteBuffer;

        struct WindowContext {
            int pendingPixelWidth = 0;
            int pendingPixelHeight = 0;
            bool resizePending = false;
            bool redrawPending = false;
            bool correctingResize = false;
            int restoredX = 0;
            int restoredY = 0;
            int restoredWidth = 800;
            int restoredHeight = 600;
        } windowContext;

        std::exception_ptr callbackError;
        Buffer uriBuffer;

        TestModeInput* testModeInput();
        int takeTestFd(int& argc, char* argv[]);
        int gridAlignedWindowSize(int framebufferSize, int border, int cellSize, float scale, int currentWindowSize);
        void queueFramebufferResize(int width, int height);
        static void childSignalHandler(int signal, siginfo_t* info, void*);
        void setupSignals();
        int startShell(const char* execPath, const char* const argv[]);
        bool keyPressed(int key);
        u16 keyboardModifiers();
        u16 inputModifiers(int modifiers);
        InputKey inputKey(int key);
        u32 decodeKeyName(const char* name);
        u32 baseLayoutKey(int key);
        void onKeyEvent(int key, int scancode, int action, int rawModifiers);
        void onTextInput(u32 codepoint, int rawModifiers);
        double pixelScaleX();
        double pixelScaleY();
        int toPixelX(double x);
        int toPixelY(double y);
        void onMouseButton(int button, bool pressed, int modifiers);
        void onMouseMotion(double x, double y);
        void onMouseWheel(double wheelX, double wheelY);
        std::string getSelectionForOsc(bool primary);
        void handleOsc(int command, const std::string& argument);
        void requestWindowAttention();
        bool presentTerminal();
        bool flushPtyOutput();
        bool readPty();
        bool servicePty(bool readable, bool writable);

        template <typename Fn>
        void guardCallback(Fn&& callback);

        void onFramebufferSize(GLFWwindow*, int width, int height);
        void onWindowRefresh(GLFWwindow*);
        void onWindowFocus(GLFWwindow*, int focused);
        void onKey(GLFWwindow*, int key, int scancode, int action, int modifiers);
        void onCharacter(GLFWwindow*, unsigned codepoint);
        void onMouseButtonCallback(GLFWwindow*, int button, int action, int modifiers);
        void onCursorPosition(GLFWwindow*, double x, double y);
        void onCursorEnter(GLFWwindow*, int entered);
        void onScroll(GLFWwindow*, double x, double y);
        void setupCallbacks();
        static ApplicationImpl& fromWindow(GLFWwindow* window);
        bool eventLoop(PtyEventSource& ptySource);
        void checkLocale();
        std::string glfwFailure(const char* operation);
        void emergencyCleanup();
    };

    struct TestModeInputImpl final: public TestModeInput {
        explicit TestModeInputImpl(ApplicationImpl* application);

        void testKeyEvent(int key, int scancode, int action, int modifiers) override;
        void testTextInput(unsigned codepoint, int modifiers) override;

        ApplicationImpl* application;
    };
}

ApplicationImpl::ApplicationImpl(Composer& composer_)
    : composer(composer_)
{
}

ApplicationImpl::~ApplicationImpl() {
    emergencyCleanup();
}

TestModeInputImpl::TestModeInputImpl(ApplicationImpl* application_)
    : application(application_)
{
}

void TestModeInputImpl::testKeyEvent(int key, int scancode, int action, int modifiers) {
    application->onKeyEvent(key, scancode, action, modifiers);
}

void TestModeInputImpl::testTextInput(unsigned codepoint, int modifiers) {
    application->onTextInput(codepoint, modifiers);
}

TestModeInput* ApplicationImpl::testModeInput() {
#ifdef SHITTY_FOR_TESTS
    return composer.pool->make<TestModeInputImpl>(this);
#else
    return nullptr;
#endif
}

int ApplicationImpl::takeTestFd(int& argc, char* argv[]) {
    for (int k = 1; k < argc; ++k) {
        if (std::strcmp(argv[k], "--test-fd") != 0) {
            continue;
        }
        if (k + 1 >= argc) {
            throw std::runtime_error("--test-fd requires a descriptor");
        }
        char* end = nullptr;
        const long fd = std::strtol(argv[k + 1], &end, 10);
        if (end == argv[k + 1] || *end || fd < 0 || fd > INT_MAX) {
            throw std::runtime_error("invalid --test-fd descriptor");
        }
        for (int j = k; j + 2 < argc; ++j) {
            argv[j] = argv[j + 2];
        }
        argc -= 2;
        argv[argc] = nullptr;
        return (int)(fd);
    }
    return -1;
}

int ApplicationImpl::gridAlignedWindowSize(int framebufferSize, int border, int cellSize, float scale, int currentWindowSize) {
    const int innerSize = framebufferSize - 2 * border;
    if (innerSize < cellSize || scale <= 0.0f) {
        return currentWindowSize;
    }

    // Wayland fractional scale is expressed in units of 1/120. Find the
    // largest cell-aligned framebuffer size representable by an integer
    // logical window size.
    const int scaleNumerator = std::max(120, (int)(std::lround(scale * 120.0f)));
    for (int cells = innerSize / cellSize; cells > 0; --cells) {
        const int framebufferTarget = 2 * border + cells * cellSize;
        const int windowTarget = (int)(((i64)(framebufferTarget) * 120 + scaleNumerator - 1) / scaleNumerator);
        if ((i64)(windowTarget)*scaleNumerator / 120 == framebufferTarget) {
            return windowTarget;
        }
    }
    return currentWindowSize;
}

void ApplicationImpl::queueFramebufferResize(int width, int height) {
    windowContext.pendingPixelWidth = width;
    windowContext.pendingPixelHeight = height;
    windowContext.resizePending = true;
}

void ApplicationImpl::childSignalHandler(int signal, siginfo_t* info, void*) {
    if (signal == SIGCHLD && info != nullptr) {
        waitpid(info->si_pid, nullptr, WNOHANG);
    }
}

void ApplicationImpl::setupSignals() {
    struct sigaction childAction{};
    childAction.sa_sigaction = childSignalHandler;
    childAction.sa_flags = SA_SIGINFO | SA_RESTART | SA_NOCLDSTOP;
    sigemptyset(&childAction.sa_mask);
    if (sigaction(SIGCHLD, &childAction, nullptr) < 0) {
        sysError("can't install SIGCHLD handler: sigaction()");
    }

    struct sigaction defaultAction{};
    defaultAction.sa_handler = SIG_DFL;
    sigemptyset(&defaultAction.sa_mask);
    if (sigaction(SIGINT, &defaultAction, nullptr) < 0) {
        sysError("can't reset SIGINT handler: sigaction()");
    }
    if (sigaction(SIGQUIT, &defaultAction, nullptr) < 0) {
        sysError("can't reset SIGQUIT handler: sigaction()");
    }
}

int ApplicationImpl::startShell(const char* execPath, const char* const argv[]) {
    int ptyFd = -1;
    const pid_t pid = pty_fork(ptyFd, opts.nCols, opts.nRows);
    if (pid < 0) {
        sysError("fork");
    }
    if (pid == 0) {
        configureTerminalChildEnvironment();
        if (execvp(execPath, (char* const*)(argv)) < 0) {
            sysError("execvp of ", execPath);
        }
    }
    return ptyFd;
}

bool ApplicationImpl::keyPressed(int key) {
    return glfwGetKey(window, key) == GLFW_PRESS;
}

u16 ApplicationImpl::keyboardModifiers() {
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

u16 ApplicationImpl::inputModifiers(int modifiers) {
    u16 result = 0;
    if (modifiers & GLFW_MOD_SHIFT) {
        result |= InputShift;
    }
    if (modifiers & GLFW_MOD_CONTROL) {
        result |= InputControl;
    }
    if (modifiers & GLFW_MOD_ALT) {
        result |= window != nullptr && keyPressed(GLFW_KEY_RIGHT_ALT) ? InputAltGraph : InputAlt;
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

InputKey ApplicationImpl::inputKey(int key) {
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

u32 ApplicationImpl::decodeKeyName(const char* name) {
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

u32 ApplicationImpl::baseLayoutKey(int key) {
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
        return key - GLFW_KEY_A + 'a';
    }
    if ((key >= GLFW_KEY_0 && key <= GLFW_KEY_9) || key == GLFW_KEY_SPACE || key == GLFW_KEY_APOSTROPHE || key == GLFW_KEY_COMMA || key == GLFW_KEY_MINUS || key == GLFW_KEY_PERIOD || key == GLFW_KEY_SLASH || key == GLFW_KEY_SEMICOLON || key == GLFW_KEY_EQUAL || key == GLFW_KEY_LEFT_BRACKET || key == GLFW_KEY_BACKSLASH || key == GLFW_KEY_RIGHT_BRACKET || key == GLFW_KEY_GRAVE_ACCENT) {
        return key;
    }
    return 0;
}

void ApplicationImpl::onKeyEvent(int key, int scancode, int action, int rawModifiers) {
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

void ApplicationImpl::onTextInput(u32 codepoint, int rawModifiers) {
    if (codepoint == 0) {
        return;
    }
    composer.input->text({codepoint, inputModifiers(rawModifiers)});
}

double ApplicationImpl::pixelScaleX() {
    int windowWidth = 0;
    int framebufferWidth = 0;
    glfwGetWindowSize(window, &windowWidth, nullptr);
    glfwGetFramebufferSize(window, &framebufferWidth, nullptr);
    if (windowWidth <= 0) {
        return 1.0;
    }
    return std::max(1.0, (double)(framebufferWidth) / windowWidth);
}

double ApplicationImpl::pixelScaleY() {
    int windowHeight = 0;
    int framebufferHeight = 0;
    glfwGetWindowSize(window, nullptr, &windowHeight);
    glfwGetFramebufferSize(window, nullptr, &framebufferHeight);
    if (windowHeight <= 0) {
        return 1.0;
    }
    return std::max(1.0, (double)(framebufferHeight) / windowHeight);
}

int ApplicationImpl::toPixelX(double x) {
    if (!std::isfinite(x)) {
        return 0;
    }
    return (int)(std::clamp(std::round(x * pixelScaleX()), (double)(INT_MIN), (double)(INT_MAX)));
}

int ApplicationImpl::toPixelY(double y) {
    if (!std::isfinite(y)) {
        return 0;
    }
    return (int)(std::clamp(std::round(y * pixelScaleY()), (double)(INT_MIN), (double)(INT_MAX)));
}

void ApplicationImpl::onMouseButton(int button, bool pressed, int modifiers) {
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

void ApplicationImpl::onMouseMotion(double x, double y) {
    composer.input->pointerMotion({toPixelX(x), toPixelY(y), keyboardModifiers()});
}

void ApplicationImpl::onMouseWheel(double wheelX, double wheelY) {
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    composer.input->scroll({wheelX, wheelY, toPixelX(x), toPixelY(y), keyboardModifiers()});
}

std::string ApplicationImpl::getSelectionForOsc(bool primary) {
    const StringView selection = primary ? composer.clipboard->readPrimary() : composer.clipboard->readClipboard();
    return std::string((const char*)(selection.data()), selection.length());
}

bool appTitleSet = false;

void ApplicationImpl::handleOsc(int command, const std::string& argument) {
    switch (command) {
        case 0:
        case 1:
        case 2:

            appTitleSet = argument != opts.title;
            glfwSetWindowTitle(window, argument.c_str());
            return;
        case 7: {
            const std::string cwd = oscCwdToPath(argument);
            if (!cwd.empty() && !appTitleSet) {
                glfwSetWindowTitle(window, cwd.c_str());
            }
            return;
        }
        case 133:
            return;
        case 52:
            break;
        default:
            return;
    }

    const Osc52Request request = parseOsc52(argument, opts.osc52SelectClipboard);
    if (!request.valid) {
        return;
    }

    if (request.query) {
        std::string primary;
        std::string clipboard;
        if (opts.allowOsc52Read) {
            if (request.primary) {
                primary = getSelectionForOsc(true);
            }
            if (primary.empty() && request.clipboard) {
                clipboard = getSelectionForOsc(false);
            }
        }
        const std::string reply = encodeOsc52QueryReply(request, opts.allowOsc52Read, primary, clipboard);
        vt->sendBytes(StringView((const u8*)(reply.data()), reply.size()), false);
        return;
    }

    const StringView content((const u8*)(request.content.data()), request.content.size());
    if (request.primary) {
        composer.clipboard->writePrimary(content);
    }
    if (request.clipboard) {
        composer.clipboard->writeClipboard(content);
    }
}

bool ApplicationImpl::presentTerminal() {
    while (true) {
        const VtermOutput output = vt->output();
        if (output.terminal == nullptr) {
            refreshPending = false;
            return true;
        }
        refreshPending = true;
        if (!refreshAllowed || !renderer->update(*output.terminal)) {
            return false;
        }
        vt->consume(VtermConsume{0, true});
    }
}

bool ApplicationImpl::flushPtyOutput() {
    while (true) {
        const VtermOutput output = vt->output();
        if (output.pty.empty()) {
            return true;
        }
        const ssize_t count = terminalPty->write(output.pty.data(), output.pty.length());
        if (count > 0) {
            vt->consume({(size_t)(count), false});
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            sysWarn("pty write");
        }
        return false;
    }
}

bool ApplicationImpl::readPty() {
    constexpr size_t maxDrainBytes = 20 * 1024 * 1024;
    u8 buffer[8192];
    size_t drained = 0;
    bool finished = false;
    while (drained < maxDrainBytes) {
        const ssize_t count = terminalPty->read(buffer, sizeof(buffer));
        if (count > 0) {
            vt->feedPty(StringView(buffer, count));
            drained += (size_t)(count);
            continue;
        }
        if (count == 0 || (count < 0 && errno == EIO)) {
            finished = true;
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        sysWarn("pty read");
        finished = true;
        break;
    }
    flushPtyOutput();
    return finished;
}

bool ApplicationImpl::servicePty(bool readable, bool writable) {
    if (writable) {
        flushPtyOutput();
    }
    return readable && readPty();
}

void ApplicationImpl::osc(int command, const std::string& argument) {
    handleOsc(command, argument);
}

bool ApplicationImpl::handlesOsc() const {
    return terminalHostReady;
}

void ApplicationImpl::bell() {
    requestWindowAttention();
}

bool ApplicationImpl::handlesPrinter() const {
    return printerPipe != nullptr;
}

void ApplicationImpl::print(const std::string& output) {
    if (printerPipe == nullptr || output.empty()) {
        return;
    }
    fwrite(output.data(), 1, output.size(), printerPipe);
    fflush(printerPipe);
}

void ApplicationImpl::leds(u8) {
}

void ApplicationImpl::notify(const std::string&, const std::string& title, const std::string& body, bool close) {
    if (close) {
        return;
    }
    requestWindowAttention();
}

void ApplicationImpl::progress(u32 state, u32) {
    if (state == 2 || state == 4) {
        requestWindowAttention();
    }
}

void ApplicationImpl::requestWindowAttention() {
    if (glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE) {
        attentionRequested = false;
        return;
    }
    if (attentionRequested) {
        return;
    }
    attentionRequested = true;
    glfwRequestWindowAttention(window);
}

void ApplicationImpl::windowOperation(u32 operation, u32 first, u32 second) {
    switch (operation) {
        case 1:
            glfwRestoreWindow(window);
            return;
        case 2:
            glfwIconifyWindow(window);
            return;
        case 3:
            glfwSetWindowPos(window, (int)(first), (int)(second));
            return;
        case 5:
            glfwFocusWindow(window);
            return;
        case 7:
            windowContext.redrawPending = true;
            glfwPostEmptyEvent();
            return;
        case 9:
            if (first == 0) {
                glfwRestoreWindow(window);
            } else if (first == 1) {
                glfwMaximizeWindow(window);
            } else if (first == 2) {
                if (glfwGetWindowAttrib(window, GLFW_MAXIMIZED)) {
                    glfwRestoreWindow(window);
                } else {
                    glfwMaximizeWindow(window);
                }
            }
            return;
        case 10: {
            const bool fullscreen = glfwGetWindowMonitor(window) != nullptr;
            const bool enable = first == 1 || (first == 2 && !fullscreen);
            if (enable == fullscreen) {
                return;
            }
            if (enable) {
                glfwGetWindowPos(window, &windowContext.restoredX, &windowContext.restoredY);
                glfwGetWindowSize(window, &windowContext.restoredWidth, &windowContext.restoredHeight);
                GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                const GLFWvidmode* mode = monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
                if (monitor != nullptr && mode != nullptr) {
                    glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
                }
            } else {
                glfwSetWindowMonitor(window, nullptr, windowContext.restoredX, windowContext.restoredY, windowContext.restoredWidth, windowContext.restoredHeight, GLFW_DONT_CARE);
            }
            return;
        }
        default:
            break;
    }

    int pixelWidth = 0;
    int pixelHeight = 0;
    if (operation == 4 && first && second) {
        pixelHeight = (int)(first);
        pixelWidth = (int)(second);
    } else if (operation == 8 && first && second) {
        pixelHeight = 2 * opts.border + (int)(first)*composer.glyphHeight;
        pixelWidth = 2 * opts.border + (int)(second)*composer.glyphWidth;
    } else {
        return;
    }
    float xScale = 1.0f;
    float yScale = 1.0f;
    glfwGetWindowContentScale(window, &xScale, &yScale);
    glfwSetWindowSize(window, std::max(1, (int)(std::ceil(pixelWidth / xScale))), std::max(1, (int)(std::ceil(pixelHeight / yScale))));
}

VtermWindowInfo ApplicationImpl::windowInfo() {
    VtermWindowInfo info;
    glfwGetWindowPos(window, &info.x, &info.y);
    GLFWmonitor* monitor = glfwGetWindowMonitor(window);
    if (monitor == nullptr) {
        monitor = glfwGetPrimaryMonitor();
    }
    const GLFWvidmode* mode = monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
    if (mode != nullptr) {
        info.screenPixelWidth = mode->width;
        info.screenPixelHeight = mode->height;
    }
    info.iconified = glfwGetWindowAttrib(window, GLFW_ICONIFIED);
    info.maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED);
    info.fullscreen = glfwGetWindowMonitor(window) != nullptr;
    return info;
}

void ApplicationImpl::wake() {
    glfwPostEmptyEvent();
}

StringView ApplicationImpl::readPrimary() {
    return StringView(primarySelection);
}

StringView ApplicationImpl::readClipboard() {
    clipboardReadBuffer.reset();
    const char* const text = glfwGetClipboardString(window);
    if (text != nullptr) {
        clipboardReadBuffer.append(text, std::strlen(text));
    }
    return StringView(clipboardReadBuffer);
}

void ApplicationImpl::writePrimary(StringView content) {
    primarySelection.reset();
    primarySelection.append(content.data(), content.length());
}

void ApplicationImpl::writeClipboard(StringView content) {
    clipboardWriteBuffer.reset();
    clipboardWriteBuffer.append(content.data(), content.length());
    glfwSetClipboardString(window, clipboardWriteBuffer.cStr());
}

void ApplicationImpl::openUri(StringView uri) {
    uriBuffer.reset();
    uriBuffer.append(uri.data(), uri.length());
    char* const path = uriBuffer.cStr();
    pid_t pid = -1;
    char* const argv[] = {
        (char*)("xdg-open"),
        path,
        nullptr,
    };
    posix_spawnp(&pid, argv[0], nullptr, nullptr, argv, environ);
}

void ApplicationImpl::pointerIcon(PointerIcon icon) {
    if (window == nullptr) {
        return;
    }
    GLFWcursor* const selected = icon == PointerIcon::Link && hyperlinkCursor != nullptr ? hyperlinkCursor : cursor;
    glfwSetCursor(window, selected);
}

template <typename Fn>
void ApplicationImpl::guardCallback(Fn&& callback) {
    if (callbackError != nullptr) {
        return;
    }
    try {
        callback();
    } catch (...) {
        callbackError = std::current_exception();
    }
}

void ApplicationImpl::onFramebufferSize(GLFWwindow*, int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    if (windowContext.correctingResize || fontpk == nullptr || glfwGetWindowMonitor(window) != nullptr || glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE) {
        queueFramebufferResize(width, height);
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
        queueFramebufferResize(width, height);
        return;
    }

    windowContext.correctingResize = true;
    glfwSetWindowSize(window, snappedWidth, snappedHeight);
    windowContext.correctingResize = false;

    // glfwSetWindowSize currently invokes the framebuffer callback
    // synchronously on Wayland. Query the final size as well so this
    // remains correct if that behavior changes.
    glfwGetFramebufferSize(window, &width, &height);
    queueFramebufferResize(width, height);
}

void ApplicationImpl::onWindowRefresh(GLFWwindow*) {
    windowContext.redrawPending = true;
}

void ApplicationImpl::onWindowFocus(GLFWwindow*, int focused) {
    guardCallback([this, focused]() {
        if (focused) {
            attentionRequested = false;
        }
        composer.input->focus(focused == GLFW_TRUE);
    });
}

void ApplicationImpl::onKey(GLFWwindow*, int key, int scancode, int action, int modifiers) {
    guardCallback([this, key, scancode, action, modifiers]() {
        onKeyEvent(key, scancode, action, modifiers);
    });
}

void ApplicationImpl::onCharacter(GLFWwindow*, unsigned codepoint) {
    guardCallback([this, codepoint]() {
        composer.input->text({codepoint, keyboardModifiers()});
    });
}

void ApplicationImpl::onMouseButtonCallback(GLFWwindow*, int button, int action, int modifiers) {
    guardCallback([this, button, action, modifiers]() {
        onMouseButton(button, action == GLFW_PRESS, modifiers);
    });
}

void ApplicationImpl::onCursorPosition(GLFWwindow*, double x, double y) {
    guardCallback([this, x, y]() {
        onMouseMotion(x, y);
    });
}

void ApplicationImpl::onCursorEnter(GLFWwindow*, int entered) {
    guardCallback([this, entered]() {
        composer.input->pointerPresence(entered == GLFW_TRUE);
    });
}

void ApplicationImpl::onScroll(GLFWwindow*, double x, double y) {
    guardCallback([this, x, y]() {
        onMouseWheel(x, y);
    });
}

void ApplicationImpl::setupCallbacks() {
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int width, int height) {
        fromWindow(w).onFramebufferSize(w, width, height);
    });
    glfwSetWindowRefreshCallback(window, [](GLFWwindow* w) {
        fromWindow(w).onWindowRefresh(w);
    });
    glfwSetWindowFocusCallback(window, [](GLFWwindow* w, int focused) {
        fromWindow(w).onWindowFocus(w, focused);
    });
    glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int scancode, int action, int modifiers) {
        fromWindow(w).onKey(w, key, scancode, action, modifiers);
    });
    glfwSetCharCallback(window, [](GLFWwindow* w, unsigned codepoint) {
        fromWindow(w).onCharacter(w, codepoint);
    });
    glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int modifiers) {
        fromWindow(w).onMouseButtonCallback(w, button, action, modifiers);
    });
    glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) {
        fromWindow(w).onCursorPosition(w, x, y);
    });
    glfwSetCursorEnterCallback(window, [](GLFWwindow* w, int entered) {
        fromWindow(w).onCursorEnter(w, entered);
    });
    glfwSetScrollCallback(window, [](GLFWwindow* w, double x, double y) {
        fromWindow(w).onScroll(w, x, y);
    });
}

ApplicationImpl& ApplicationImpl::fromWindow(GLFWwindow* window) {
    return *(ApplicationImpl*)(glfwGetWindowUserPointer(window));
}

bool ApplicationImpl::eventLoop(PtyEventSource& ptySource) {
    using Clock = std::chrono::steady_clock;
    constexpr auto resizeGrace = std::chrono::milliseconds(10);
    constexpr auto retryDelay = std::chrono::milliseconds(10);
    while (!glfwWindowShouldClose(window)) {
        ptySource.setWriteInterest(!vt->output().pty.empty());
        refreshAllowed = false;
        const VtermState initialState = vt->state();
        double timeout = (initialState.synchronizedOutput || initialState.animation) ? 0.05 : -1.0;
        if (refreshPending || committedRepaintPending) {
            const auto now = Clock::now();
            const double refreshTimeout = refreshDeadline ? std::max(0.0, std::chrono::duration<double>(*refreshDeadline - now).count()) : 0.0;
            timeout = timeout < 0.0 ? refreshTimeout : std::min(timeout, refreshTimeout);
        }
        if (timeout == 0.0) {
            glfwPollEvents();
        } else if (timeout > 0.0) {
            glfwWaitEventsTimeout(timeout);
        } else {
            glfwWaitEvents();
        }
        if (callbackError != nullptr) {
            std::rethrow_exception(callbackError);
        }
        composer.input->flush();
        if (glfwWindowShouldClose(window)) {
            return true;
        }
        flushPtyOutput();
        vt->expireSynchronizedOutput(false);
        if (vt->advanceAnimation(false)) {
            vt->expose();
        }
        bool resized = false;
        if (windowContext.resizePending) {
            const int width = std::min(windowContext.pendingPixelWidth, (int)(UINT16_MAX));
            const int height = std::min(windowContext.pendingPixelHeight, (int)(UINT16_MAX));
            windowContext.resizePending = false;
            windowContext.redrawPending = false;
            composer.resize(width, height);
            const VtermState resizedState = vt->state();
            committedRepaintPending = resizedState.synchronizedOutput;
            resized = true;
            refreshDeadline = Clock::now() + resizeGrace;
        } else if (windowContext.redrawPending) {
            windowContext.redrawPending = false;
            if (vt->state().synchronizedOutput) {
                committedRepaintPending = true;
            } else {
                vt->expose();
            }
        }
        const short ptyEvents = ptySource.events();
        const bool readPtyInput = ptyEvents & (POLLIN | POLLHUP | POLLERR);
        const bool finished = servicePty(readPtyInput, ptyEvents & POLLOUT);
        if (readPtyInput) {
            ptySource.acknowledge();
            if (finished) {
                return false;
            }
        } else if (ptyEvents && !(ptyEvents & (POLLIN | POLLHUP | POLLERR))) {
            ptySource.acknowledge();
        }
        flushPtyOutput();
        ptySource.setWriteInterest(!vt->output().pty.empty());
        presentTerminal();

        // A child responding to SIGWINCH is part of the same visual
        // update. Give it a short opportunity to redraw, then fall back
        // to the resized terminal state even if it produces no output.
        if (readPtyInput) {
            refreshDeadline.reset();
        }
        const auto now = Clock::now();
        if (!vt->state().synchronizedOutput) {
            committedRepaintPending = false;
        }
        if (committedRepaintPending && (!refreshDeadline || now >= *refreshDeadline)) {
            if (renderer->repaint()) {
                committedRepaintPending = false;
                refreshDeadline.reset();
            } else {
                refreshDeadline = now + retryDelay;
            }
        }
        if (refreshPending && (!refreshDeadline || now >= *refreshDeadline)) {
            refreshAllowed = true;
            presentTerminal();
            refreshAllowed = false;
            if (refreshPending) {
                refreshDeadline = now + retryDelay;
            } else {
                refreshDeadline.reset();
            }
        } else if (resized && !refreshPending) {
            refreshDeadline.reset();
        }
    }
    refreshAllowed = true;
    return true;
}

void ApplicationImpl::checkLocale() {
    const char* locale = setlocale(LC_ALL, "");
    if (locale == nullptr) {
        sysO << StringView(u8"Warning: could not set locale; international input may be broken.") << endL;
        return;
    }
    if (std::strcmp(nl_langinfo(CODESET), "UTF-8") != 0) {
        sysO << StringView(u8"Warning: non-UTF-8 locale ") << StringView(locale) << StringView(u8"; international input may be broken.") << endL;
    }
}

std::string ApplicationImpl::glfwFailure(const char* operation) {
    const char* description = nullptr;
    const int code = glfwGetError(&description);
    std::string message = operation;
    message += " failed";
    if (description != nullptr) {
        message += ": ";
        message += description;
    } else if (code != GLFW_NO_ERROR) {
        message += " (GLFW error ";
        message += std::to_string(code);
        message += ')';
    }
    return message;
}

int ApplicationImpl::run(int argc, char* argv[]) {
    TestModeInput* const testInput = testModeInput();
    const int testFd = testInput == nullptr ? -1 : takeTestFd(argc, argv);
    checkLocale();
    opts.initialize(&argc, argv);
    opts.parse();
    if (opts.verbose) {
        opts.printVersion();
    }
    if (setenv("SHITTY_VERSION", SHITTY_VERSION, 1) < 0) {
        sysError("setenv SHITTY_VERSION");
    }
    if (testFd >= 0) {
        return runTestMode(composer, *testInput, testFd, argc, argv);
    }

    LaunchCommand launch = buildLaunchCommand(argc, argv, opts.shell, opts.login);
    if (argc > 2 && std::strcmp(argv[1], "-e") == 0) {
        if (opts.titleSource != OptionSource::CmdLine) {
            opts.title = argv[2];
        }
    }
    std::vector<char*> shellArgv;
    for (auto& argument : launch.arguments) {
        shellArgv.push_back(argument.data());
    }
    shellArgv.push_back(nullptr);

    setupSignals();
    const int ptyFd = startShell(launch.executable.c_str(), shellArgv.data());
    terminalPty = Pty::adopt(composer, ptyFd);
    composer.pty = terminalPty;

    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
    if (!glfwInit()) {
        throw std::runtime_error(glfwFailure("glfwInit"));
    }
    glfwInitialized = true;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, "shitty");

    const int initialWidth = std::max(320, (int)(opts.nCols) * opts.fontsize / 2);
    const int initialHeight = std::max(200, (int)(opts.nRows) * opts.fontsize);
    window = glfwCreateWindow(initialWidth, initialHeight, opts.title, nullptr, nullptr);
    if (window == nullptr) {
        throw std::runtime_error(glfwFailure("glfwCreateWindow"));
    }
    glfwSetWindowUserPointer(window, this);
    composer.clipboard = this;
    glfwSetInputMode(window, GLFW_LOCK_KEY_MODS, GLFW_TRUE);

    float xScale = 1.0f;
    float yScale = 1.0f;
    glfwGetWindowContentScale(window, &xScale, &yScale);
    const float density = std::max({1.0f, xScale, yScale});
    opts.fontsize = (u8)(std::clamp((int)(std::lround(opts.fontsize * density)), 1, 255));
    opts.border = (u16)(std::clamp((int)(std::lround(opts.border * density)), 0, 3000));

    fontpk = Fontpack::create(composer, opts.fontname, opts.dwfontname);
    composer.fonts = fontpk;
    composer.setGlyphSize(fontpk->getPx(), fontpk->getPy());
    const int desiredPixelWidth = 2 * opts.border + opts.nCols * composer.glyphWidth;
    const int desiredPixelHeight = 2 * opts.border + opts.nRows * composer.glyphHeight;
    const int desiredWidth = std::max(1, (int)(std::ceil(desiredPixelWidth / density)));
    const int desiredHeight = std::max(1, (int)(std::ceil(desiredPixelHeight / density)));
    glfwSetWindowSizeLimits(window, std::max(1, (int)(std::ceil((2 * opts.border + composer.glyphWidth) / density))), std::max(1, (int)(std::ceil((2 * opts.border + composer.glyphHeight) / density))), GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwSetWindowSize(window, desiredWidth, desiredHeight);
    glfwShowWindow(window);
    glfwPollEvents();

    int pixelWidth = 0;
    int pixelHeight = 0;
    glfwGetFramebufferSize(window, &pixelWidth, &pixelHeight);
    if (pixelWidth <= 0 || pixelHeight <= 0) {
        throw std::runtime_error("Initial framebuffer has invalid size");
    }
    if (pixelWidth > UINT16_MAX || pixelHeight > UINT16_MAX) {
        throw std::runtime_error("Initial window exceeds terminal limits");
    }
    composer.resize(pixelWidth, pixelHeight);

    cursor = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    hyperlinkCursor = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
    composer.desktopActions = this;
    if (cursor != nullptr) {
        glfwSetCursor(window, cursor);
    }

    renderer = Renderer::create(composer, window);
    composer.renderer = renderer;
    if (opts.printerCommand != nullptr && opts.printerCommand[0] != '\0') {
        printerPipe = popen(opts.printerCommand, "w");
        if (printerPipe == nullptr) {
            throw std::runtime_error("Cannot start printer command");
        }
    }
    vt = Vterm::create(composer, *this, nullptr);
    composer.vterm = vt;
    terminalHostReady = true;
    setupCallbacks();
    composer.input->focus(glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE);
    presentTerminal();

    PtyEventSource* ptySource = PtyEventSource::create(composer, *terminalPty, *this);
    composer.ptyEvents = ptySource;
    eventLoop(*ptySource);

    vt = nullptr;
    composer.vterm = nullptr;
    composer.ptyEvents = nullptr;
    if (printerPipe != nullptr) {
        pclose(printerPipe);
        printerPipe = nullptr;
    }
    composer.pty = nullptr;
    terminalPty = nullptr;
    renderer = nullptr;
    composer.renderer = nullptr;
    fontpk = nullptr;
    composer.fonts = nullptr;
    return 0;
}

void ApplicationImpl::emergencyCleanup() {
    vt = nullptr;
    composer.clipboard = nullptr;
    composer.desktopActions = nullptr;
    if (printerPipe != nullptr) {
        pclose(printerPipe);
        printerPipe = nullptr;
    }
    renderer = nullptr;
    fontpk = nullptr;
    if (cursor != nullptr) {
        glfwDestroyCursor(cursor);
        cursor = nullptr;
    }
    if (hyperlinkCursor != nullptr) {
        glfwDestroyCursor(hyperlinkCursor);
        hyperlinkCursor = nullptr;
    }
    if (window != nullptr) {
        glfwDestroyWindow(window);
        window = nullptr;
    }
    if (glfwInitialized) {
        glfwTerminate();
        glfwInitialized = false;
    }
}

Application* Application::create(Composer& composer) {
    return composer.pool->make<ApplicationImpl>(composer);
}
