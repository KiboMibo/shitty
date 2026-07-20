/* This file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "application.h"
#include "base.h"
#include "clipboard.h"
#include "composer.h"
#include "font_pack.h"
#include "keyboard.h"
#include "log.h"
#include "mouse_frontend.h"
#include "mouse_protocol.h"
#include "osc_protocol.h"
#include "options.h"
#include "pty.h"
#include "pty_event_source.h"
#include "vk_renderer.h"
#include "startup.h"
#include "test_mode.h"
#include "utf8.h"
#include "vterm.h"
#include "vterm_host.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

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

extern char** environ;

namespace {
    class ApplicationImpl final: public Application, public VtermHost, public PtyEventHost {
    public:
        explicit ApplicationImpl(Composer& composer);
        ~ApplicationImpl();

        int run(int argc, char* argv[]) override;
        bool present(const Frame& frame) override;
        void osc(int command, const std::string& argument) override;
        bool handlesOsc() const override;
        void bell() override;
        void print(const std::string& output) override;
        void leds(u8) override;
        void notify(const std::string&, const std::string& title, const std::string& body, bool close) override;
        void progress(u32 state, u32) override;
        void windowOperation(u32 operation, u32 first, u32 second) override;
        VtermWindowInfo windowInfo() override;
        void wake() override;

        Composer& composer;
        Fontpack* fontpk = nullptr;
        Renderer* renderer = nullptr;
        Vterm* vt = nullptr;
        GLFWwindow* window = nullptr;
        GLFWcursor* cursor = nullptr;
        GLFWcursor* hyperlinkCursor = nullptr;
        bool glfwInitialized = false;
        FILE* printerPipe = nullptr;
        bool refreshAllowed = true;
        bool refreshPending = false;
        bool committedRepaintPending = false;
        bool terminalHostReady = false;
        std::optional<std::chrono::steady_clock::time_point> refreshDeadline;
        ClipboardStore clipboardStore;

        struct MouseContext {
            MouseFrontendState frontend;
            bool hyperlinkClick = false;
            bool cursorInside = true;
        } mouseContext;

        struct WindowContext {
            int framebufferWidth = 0;
            int framebufferHeight = 0;
            bool resizePending = false;
            bool redrawPending = false;
            bool correctingResize = false;
            int restoredX = 0;
            int restoredY = 0;
            int restoredWidth = 800;
            int restoredHeight = 600;
        } windowContext;

        unsigned suppressedTextInputs = 0;
        bool locallyConsumedKeys[GLFW_KEY_LAST + 1]{};

        struct PendingKittyTextKey {
            bool active = false;
            u32 primary = 0;
            u32 base = 0;
            u16 modifiers = 0;
            Vterm::KeyEventType event = Vterm::KeyEventType::Press;
        } pendingKittyTextKey;

        std::exception_ptr callbackError;

        int takeTestFd(int& argc, char* argv[]);
        int gridAlignedWindowSize(int framebufferSize, int border, int cellSize, float scale, int currentWindowSize);
        void queueFramebufferResize(int width, int height);
        static void childSignalHandler(int signal, siginfo_t* info, void*);
        void setupSignals();
        int startShell(const char* execPath, const char* const argv[]);
        bool keyPressed(int key);
        int keyboardModifiers();
        VtModifier convertModifiers(int modifiers);
        int significantModifiers(int modifiers);
        u16 kittyModifiers(int modifiers);
        u32 decodeKeyName(const char* name);
        u32 baseLayoutKey(int key);
        void flushPendingKittyTextKey();
        bool pasteSelection(bool primary);
        bool copyPrimaryToClipboard();
        VtKey keypadKey(int key, bool numLock);
        VtKey specialKey(int key, int modifiers);
        void onKeyEvent(int key, int scancode, int action, int rawModifiers);
        void onTextInput(u32 codepoint);
        double pixelScaleX();
        double pixelScaleY();
        int toPixelX(double x);
        int toPixelY(double y);
        bool isMouseProtocol(int modifiers, const MouseTrackingState& tracking);
        void mouseProtocolCoordinates(MouseTrackingEnc encoding, int pixelX, int pixelY, u16& column, u16& row);
        void mouseProtocolSend(MouseTrackingEnc encoding, MouseEventType type, int modifiers, int button, int column, int row);
        void sendMouseButtonProtocol(MouseEventType type, int button, int pixelX, int pixelY, int modifiers, const MouseTrackingState& tracking);
        bool isMultipleClick(int button, double x, double y);
        void openHyperlink(const std::string& uri);
        void onMouseButton(int button, bool pressed, int modifiers);
        void onMouseMotion(double x, double y);
        void onMouseWheel(double wheelX, double wheelY);
        std::string getSelectionForOsc(bool primary);
        void handleOsc(int command, const std::string& argument);

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
}

static_assert(GLFW_MOD_SHIFT == FrontendShift);
static_assert(GLFW_MOD_CONTROL == FrontendControl);
static_assert(GLFW_MOD_ALT == FrontendAlt);

ApplicationImpl::ApplicationImpl(Composer& composer_)
    : composer(composer_)
{
}

ApplicationImpl::~ApplicationImpl() {
    emergencyCleanup();
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
    windowContext.framebufferWidth = width;
    windowContext.framebufferHeight = height;
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
        SYS_ERROR("can't install SIGCHLD handler: sigaction()");
    }

    struct sigaction defaultAction{};
    defaultAction.sa_handler = SIG_DFL;
    sigemptyset(&defaultAction.sa_mask);
    if (sigaction(SIGINT, &defaultAction, nullptr) < 0) {
        SYS_ERROR("can't reset SIGINT handler: sigaction()");
    }
    if (sigaction(SIGQUIT, &defaultAction, nullptr) < 0) {
        SYS_ERROR("can't reset SIGQUIT handler: sigaction()");
    }
}

int ApplicationImpl::startShell(const char* execPath, const char* const argv[]) {
    int ptyFd = -1;
    const pid_t pid = pty_fork(ptyFd, opts.nCols, opts.nRows);
    if (pid < 0) {
        SYS_ERROR("fork");
    }
    if (pid == 0) {
        configureTerminalChildEnvironment();
        if (execvp(execPath, (char* const*)(argv)) < 0) {
            SYS_ERROR("execvp of ", execPath);
        }
    }
    logT << "Shell subprocess started, pid: " << pid << std::endl;
    return ptyFd;
}

bool ApplicationImpl::keyPressed(int key) {
    return glfwGetKey(window, key) == GLFW_PRESS;
}

int ApplicationImpl::keyboardModifiers() {
    int modifiers = 0;
    if (keyPressed(GLFW_KEY_LEFT_SHIFT) || keyPressed(GLFW_KEY_RIGHT_SHIFT)) {
        modifiers |= GLFW_MOD_SHIFT;
    }
    if (keyPressed(GLFW_KEY_LEFT_CONTROL) || keyPressed(GLFW_KEY_RIGHT_CONTROL)) {
        modifiers |= GLFW_MOD_CONTROL;
    }
    if (keyPressed(GLFW_KEY_LEFT_ALT)) {
        modifiers |= GLFW_MOD_ALT;
    }
    if (keyPressed(GLFW_KEY_LEFT_SUPER) || keyPressed(GLFW_KEY_RIGHT_SUPER)) {
        modifiers |= GLFW_MOD_SUPER;
    }
    return modifiers;
}

VtModifier ApplicationImpl::convertModifiers(int modifiers) {
    VtModifier result = VtModifier::none;
    if (modifiers & GLFW_MOD_SHIFT) {
        result = result | VtModifier::shift;
    }
    if (modifiers & GLFW_MOD_CONTROL) {
        result = result | VtModifier::control;
    }
    if ((modifiers & GLFW_MOD_ALT) && !keyPressed(GLFW_KEY_RIGHT_ALT)) {
        result = result | VtModifier::alt;
    }
    if ((modifiers & GLFW_MOD_SUPER) && vt != nullptr && vt->getMetaMode()) {
        // Xterm resolves Meta through the platform modifier mapping.  GLFW
        // exposes the closest portable Meta modifier as Super.
        result = result | VtModifier::alt;
    }
    return result;
}

int ApplicationImpl::significantModifiers(int modifiers) {
    return modifiers & (GLFW_MOD_SHIFT | GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER);
}

u16 ApplicationImpl::kittyModifiers(int modifiers) {
    u16 result = 0;
    if (modifiers & GLFW_MOD_SHIFT) {
        result |= 1;
    }
    if ((modifiers & GLFW_MOD_ALT) && !keyPressed(GLFW_KEY_RIGHT_ALT)) {
        result |= 2;
    }
    if (modifiers & GLFW_MOD_CONTROL) {
        result |= 4;
    }
    if (modifiers & GLFW_MOD_SUPER) {
        result |= 8;
    }
    if (modifiers & GLFW_MOD_CAPS_LOCK) {
        result |= 64;
    }
    if (modifiers & GLFW_MOD_NUM_LOCK) {
        result |= 128;
    }
    return result;
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

void ApplicationImpl::flushPendingKittyTextKey() {
    if (!pendingKittyTextKey.active) {
        return;
    }
    const auto pending = pendingKittyTextKey;
    pendingKittyTextKey.active = false;
    vt->writeKittyKey(pending.primary, 0, pending.base, pending.modifiers, pending.event);
}

bool ApplicationImpl::pasteSelection(bool primary) {
    const std::string text = clipboardStore.get(primary);
    if (text.empty()) {
        return false;
    }
    vt->pasteSelection(text);
    return true;
}

bool ApplicationImpl::copyPrimaryToClipboard() {
    return clipboardStore.copyPrimaryToClipboard();
}

VtKey ApplicationImpl::keypadKey(int key, bool numLock) {
    using Key = VtKey;
    if (!numLock) {
        switch (key) {
            case GLFW_KEY_KP_0:
                return Key::KP_Insert;
            case GLFW_KEY_KP_1:
                return Key::KP_End;
            case GLFW_KEY_KP_2:
                return Key::KP_Down;
            case GLFW_KEY_KP_3:
                return Key::KP_PageDown;
            case GLFW_KEY_KP_4:
                return Key::KP_Left;
            case GLFW_KEY_KP_5:
                return Key::KP_Begin;
            case GLFW_KEY_KP_6:
                return Key::KP_Right;
            case GLFW_KEY_KP_7:
                return Key::KP_Home;
            case GLFW_KEY_KP_8:
                return Key::KP_Up;
            case GLFW_KEY_KP_9:
                return Key::KP_PageUp;
            case GLFW_KEY_KP_DECIMAL:
                return Key::KP_Delete;
            default:
                break;
        }
    }

    switch (key) {
        case GLFW_KEY_KP_0:
            return Key::KP_0;
        case GLFW_KEY_KP_1:
            return Key::KP_1;
        case GLFW_KEY_KP_2:
            return Key::KP_2;
        case GLFW_KEY_KP_3:
            return Key::KP_3;
        case GLFW_KEY_KP_4:
            return Key::KP_4;
        case GLFW_KEY_KP_5:
            return Key::KP_5;
        case GLFW_KEY_KP_6:
            return Key::KP_6;
        case GLFW_KEY_KP_7:
            return Key::KP_7;
        case GLFW_KEY_KP_8:
            return Key::KP_8;
        case GLFW_KEY_KP_9:
            return Key::KP_9;
        case GLFW_KEY_KP_DECIMAL:
            return Key::KP_Dot;
        case GLFW_KEY_KP_DIVIDE:
            return Key::KP_Slash;
        case GLFW_KEY_KP_MULTIPLY:
            return Key::KP_Star;
        case GLFW_KEY_KP_SUBTRACT:
            return Key::KP_Minus;
        case GLFW_KEY_KP_ADD:
            return Key::KP_Plus;
        case GLFW_KEY_KP_ENTER:
            return Key::KP_Enter;
        case GLFW_KEY_KP_EQUAL:
            return Key::KP_Equal;
        default:
            return Key::NONE;
    }
}

VtKey ApplicationImpl::specialKey(int key, int modifiers) {
    using Key = VtKey;
    const Key keypad = keypadKey(key, (modifiers & GLFW_MOD_NUM_LOCK) != 0);
    if (keypad != Key::NONE) {
        return keypad;
    }

    switch (key) {
        case GLFW_KEY_ENTER:
            return Key::Return;
        case GLFW_KEY_BACKSPACE:
            return Key::Backspace;
        case GLFW_KEY_TAB:
            return Key::Tab;
        case GLFW_KEY_INSERT:
            return Key::Insert;
        case GLFW_KEY_DELETE:
            return Key::Delete;
        case GLFW_KEY_HOME:
            return Key::Home;
        case GLFW_KEY_END:
            return Key::End;
        case GLFW_KEY_UP:
            return Key::Up;
        case GLFW_KEY_DOWN:
            return Key::Down;
        case GLFW_KEY_LEFT:
            return Key::Left;
        case GLFW_KEY_RIGHT:
            return Key::Right;
        case GLFW_KEY_PAGE_UP:
            return Key::PageUp;
        case GLFW_KEY_PAGE_DOWN:
            return Key::PageDown;
        case GLFW_KEY_F1:
            return Key::F1;
        case GLFW_KEY_F2:
            return Key::F2;
        case GLFW_KEY_F3:
            return Key::F3;
        case GLFW_KEY_F4:
            return Key::F4;
        case GLFW_KEY_F5:
            return Key::F5;
        case GLFW_KEY_F6:
            return Key::F6;
        case GLFW_KEY_F7:
            return Key::F7;
        case GLFW_KEY_F8:
            return Key::F8;
        case GLFW_KEY_F9:
            return Key::F9;
        case GLFW_KEY_F10:
            return Key::F10;
        case GLFW_KEY_F11:
            return Key::F11;
        case GLFW_KEY_F12:
            return Key::F12;
        case GLFW_KEY_F13:
            return Key::F13;
        case GLFW_KEY_F14:
            return Key::F14;
        case GLFW_KEY_F15:
            return Key::F15;
        case GLFW_KEY_F16:
            return Key::F16;
        case GLFW_KEY_F17:
            return Key::F17;
        case GLFW_KEY_F18:
            return Key::F18;
        case GLFW_KEY_F19:
            return Key::F19;
        case GLFW_KEY_F20:
            return Key::F20;
        case GLFW_KEY_CAPS_LOCK:
            return Key::CapsLock;
        case GLFW_KEY_SCROLL_LOCK:
            return Key::ScrollLock;
        case GLFW_KEY_NUM_LOCK:
            return Key::NumLock;
        case GLFW_KEY_PRINT_SCREEN:
            return Key::Print;
        case GLFW_KEY_PAUSE:
            return Key::Pause;
        case GLFW_KEY_MENU:
            return Key::Menu;
        case GLFW_KEY_LEFT_SHIFT:
            return Key::LeftShift;
        case GLFW_KEY_LEFT_CONTROL:
            return Key::LeftControl;
        case GLFW_KEY_LEFT_ALT:
            return Key::LeftAlt;
        case GLFW_KEY_LEFT_SUPER:
            return Key::LeftSuper;
        case GLFW_KEY_RIGHT_SHIFT:
            return Key::RightShift;
        case GLFW_KEY_RIGHT_CONTROL:
            return Key::RightControl;
        case GLFW_KEY_RIGHT_ALT:
            return Key::RightAlt;
        case GLFW_KEY_RIGHT_SUPER:
            return Key::RightSuper;
        default:
            return Key::NONE;
    }
}

void ApplicationImpl::onKeyEvent(int key, int scancode, int action, int rawModifiers) {
    flushPendingKittyTextKey();
    const int keyModifiers = rawModifiers;
    const int legacyModifiers = significantModifiers(rawModifiers);
    const VtModifier modifiers = convertModifiers(legacyModifiers);
    const bool pressed = action != GLFW_RELEASE;
    const bool validKey = key >= 0 && key <= GLFW_KEY_LAST;
    if (!pressed && validKey && locallyConsumedKeys[key]) {
        locallyConsumedKeys[key] = false;
        return;
    }
    const auto runLocal = [&](const auto& operation) {
        if (pressed) {
            if (validKey) {
                locallyConsumedKeys[key] = true;
            }
            operation();
        }
    };

    if (key == GLFW_KEY_PAGE_UP && modifiers == VtModifier::shift) {
        runLocal([&]() {
            vt->pageUp();
        });
        return;
    }
    if (key == GLFW_KEY_PAGE_DOWN && modifiers == VtModifier::shift) {
        runLocal([&]() {
            vt->pageDown();
        });
        return;
    }
    if (key == GLFW_KEY_C && modifiers == VtModifier::shift_control) {
        runLocal([&]() {
            copyPrimaryToClipboard();
        });
        return;
    }
    if (key == GLFW_KEY_V && modifiers == VtModifier::shift_control) {
        runLocal([&]() {
            pasteSelection(false);
        });
        return;
    }
    if ((key == GLFW_KEY_INSERT || key == GLFW_KEY_KP_0) && modifiers == VtModifier::shift) {
        runLocal([&]() {
            pasteSelection(true);
        });
        return;
    }
    if (key == GLFW_KEY_SPACE && mouseContext.frontend.selectionOngoing()) {
        runLocal([&]() {
            vt->selectRectangularModeToggle();
        });
        return;
    }

    const u8 kittyFlags = vt->getKittyKeyboardFlags();
    const u16 kittyMods = kittyModifiers(rawModifiers);
    const auto event = action == GLFW_RELEASE ? Vterm::KeyEventType::Release : action == GLFW_REPEAT ? Vterm::KeyEventType::Repeat : Vterm::KeyEventType::Press;

    if (kittyFlags) {
        if (key == GLFW_KEY_ESCAPE) {
            vt->writeKittyKey(27, 0, 0, kittyMods, event);
            return;
        }

        const VtKey special = specialKey(key, keyModifiers);
        if (special != VtKey::NONE) {
            vt->writeKittyKey(special, kittyMods, event);
            return;
        }

        const u32 baseKey = baseLayoutKey(key);
        u32 primaryKey = decodeKeyName(glfwGetKeyName(key, scancode));
        if (!primaryKey) {
            primaryKey = baseKey;
        }
        const u16 textMods = kittyMods & ~(64 | 128);
        if (primaryKey && ((textMods & (2 | 4 | 8)) || (kittyFlags & 0x08))) {
            if (pressed && !(textMods & (2 | 4 | 8))) {
                pendingKittyTextKey = {true, primaryKey, baseKey, textMods, event};
                return;
            } else {
                vt->writeKittyKey(primaryKey, 0, baseKey, textMods, event);
            }
            if (pressed && (((textMods & (2 | 8)) && !(textMods & 4)) || (kittyFlags & 0x08))) {
                ++suppressedTextInputs;
            }
            return;
        }
    }

    if (!pressed) {
        return;
    }

    if (key == GLFW_KEY_ESCAPE) {
        vt->writePty((u8)('\x1b'), modifiers, true);
        return;
    }

    const VtKey special = specialKey(key, keyModifiers);
    if (special != VtKey::NONE) {
        vt->writePty(special, modifiers, true);
        return;
    }

    if (legacyModifiers & GLFW_MOD_CONTROL) {
        u8 character = 0;
        if (controlCharacter(key, legacyModifiers & GLFW_MOD_SHIFT, character)) {
            vt->writePty(character, modifiers, true);
        }
    }
}

void ApplicationImpl::onTextInput(u32 codepoint) {
    if (pendingKittyTextKey.active) {
        const auto pending = pendingKittyTextKey;
        pendingKittyTextKey.active = false;
        const u32 alternate = codepoint != pending.primary ? codepoint : 0;
        vt->writeKittyKey(pending.primary, alternate, pending.base, pending.modifiers, pending.event);
        return;
    }
    if (suppressedTextInputs) {
        --suppressedTextInputs;
        return;
    }
    if (codepoint == 0) {
        return;
    }
    const int rawModifiers = keyboardModifiers();
    const VtModifier modifiers = convertModifiers(rawModifiers);

    if (codepoint < 0x80) {
        vt->writePty((u8)(codepoint), modifiers, true);
        return;
    }

    std::string text;
    Utf8Encoder::pushUnicode(codepoint, [&text](u8 byte) {
        text.push_back((char)(byte));
    });
    if ((rawModifiers & GLFW_MOD_ALT) && opts.altSendsEscape) {
        vt->writePty("\x1b", true);
    }
    vt->writePty(text.c_str(), true);
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
    return mouseFramebufferCoordinate(x, pixelScaleX());
}

int ApplicationImpl::toPixelY(double y) {
    return mouseFramebufferCoordinate(y, pixelScaleY());
}

bool ApplicationImpl::isMouseProtocol(int modifiers, const MouseTrackingState& tracking) {
    return mouseContext.frontend.protocolActive(modifiers, tracking.mode);
}

void ApplicationImpl::mouseProtocolCoordinates(MouseTrackingEnc encoding, int pixelX, int pixelY, u16& column, u16& row) {
    const MouseProtocolPoint point = mouseProtocolPoint(encoding, pixelX, pixelY, {windowContext.framebufferWidth, windowContext.framebufferHeight, opts.border, fontpk->getPx(), fontpk->getPy()});
    column = point.column;
    row = point.row;
}

void ApplicationImpl::mouseProtocolSend(MouseTrackingEnc encoding, MouseEventType type, int modifiers, int button, int column, int row) {
    const unsigned protocolModifiers = mouseProtocolModifiers(modifiers, !keyPressed(GLFW_KEY_RIGHT_ALT));
    const int motionButton = mouseContext.frontend.motionButton();
    vt->writePty(encodeMouseProtocol(encoding, type, protocolModifiers, motionButton, button, column, row).c_str());
}

void ApplicationImpl::sendMouseButtonProtocol(MouseEventType type, int button, int pixelX, int pixelY, int modifiers, const MouseTrackingState& tracking) {
    if (!mouseButtonReportAllowed(tracking.mode, type, button)) {
        return;
    }

    u16 column = 0;
    u16 row = 0;
    mouseProtocolCoordinates(tracking.enc, pixelX, pixelY, column, row);
    if (tracking.mode == MouseTrackingMode::VT200_Highlight && type == MouseEventType::Release) {
        vt->mouseHighlightRelease(column, row, column, row);
        return;
    }
    const int protocolModifiers = tracking.mode == MouseTrackingMode::X10_Compat ? 0 : modifiers;
    mouseProtocolSend(tracking.enc, type, protocolModifiers, button, column, row);
}

bool ApplicationImpl::isMultipleClick(int button, double x, double y) {
    return mouseContext.frontend.registerClick(button, x, y, glfwGetTime()) > 1;
}

void ApplicationImpl::openHyperlink(const std::string& uri) {
    pid_t pid = -1;
    char* const argv[] = {
        (char*)("xdg-open"),
        (char*)(uri.c_str()),
        nullptr,
    };
    const int error = posix_spawnp(&pid, argv[0], nullptr, nullptr, argv, environ);
    if (error != 0) {
        logW << "Cannot open hyperlink '" << uri << "': " << strerror(error) << std::endl;
    }
}

void ApplicationImpl::onMouseButton(int button, bool pressed, int modifiers) {
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    const int pixelX = toPixelX(x);
    const int pixelY = toPixelY(y);
    mouseContext.frontend.updateButton(button, pressed);
    const auto& tracking = vt->getMouseTrackingState();
    const int protocolButton = mouseTerminalButton(button);
    u16 locatorColumn = 1;
    u16 locatorRow = 1;
    mouseProtocolCoordinates(MouseTrackingEnc::Default, pixelX, pixelY, locatorColumn, locatorRow);
    vt->setLocatorPosition(locatorColumn, locatorRow, std::max(1, pixelX + 1), std::max(1, pixelY + 1));
    if (protocolButton >= 1 && protocolButton <= 4) {
        vt->reportLocatorButton(protocolButton, pressed);
    }

    if (!pressed && button == GLFW_MOUSE_BUTTON_LEFT && mouseContext.hyperlinkClick) {
        mouseContext.hyperlinkClick = false;
        return;
    }
    if (pressed && button == GLFW_MOUSE_BUTTON_LEFT && (modifiers & GLFW_MOD_CONTROL)) {
        const std::string uri = vt->getHyperlink(pixelX, pixelY);
        if (!uri.empty()) {
            mouseContext.hyperlinkClick = true;
            openHyperlink(uri);
            return;
        }
    }

    if (isMouseProtocol(modifiers, tracking)) {
        sendMouseButtonProtocol(pressed ? MouseEventType::Press : MouseEventType::Release, protocolButton, pixelX, pixelY, modifiers, tracking);
        return;
    }

    if (pressed) {
        const bool cycleSnapTo = isMultipleClick(button, x, y);
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            vt->selectStart(pixelX, pixelY, cycleSnapTo);
            mouseContext.frontend.beginSelection();
        } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            vt->selectExtend(pixelX, pixelY, cycleSnapTo);
            mouseContext.frontend.beginSelection();
        }
        return;
    }

    if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_RIGHT) {
        std::string selection;
        mouseContext.frontend.endSelection();
        if (vt->selectFinish(selection)) {
            clipboardStore.setPrimary(selection, opts.autoCopyMode);
        }
    } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        pasteSelection(true);
    }
}

void ApplicationImpl::onMouseMotion(double x, double y) {
    const int pixelX = toPixelX(x);
    const int pixelY = toPixelY(y);
    const int modifiers = keyboardModifiers();
    u16 locatorColumn = 1;
    u16 locatorRow = 1;
    mouseProtocolCoordinates(MouseTrackingEnc::Default, pixelX, pixelY, locatorColumn, locatorRow);
    vt->setLocatorPosition(locatorColumn, locatorRow, std::max(1, pixelX + 1), std::max(1, pixelY + 1));
    const bool overHyperlink = (modifiers & GLFW_MOD_CONTROL) && !vt->getHyperlink(pixelX, pixelY).empty();
    glfwSetCursor(window, overHyperlink && hyperlinkCursor != nullptr ? hyperlinkCursor : cursor);
    const auto& tracking = vt->getMouseTrackingState();
    if (isMouseProtocol(modifiers, tracking)) {
        if (tracking.mode == MouseTrackingMode::VT200_ButtonEvent && !mouseContext.frontend.primaryButtonPressed()) {
            return;
        }
        if (tracking.mode != MouseTrackingMode::VT200_ButtonEvent && tracking.mode != MouseTrackingMode::VT200_AnyEvent) {
            return;
        }

        u16 column = 0;
        u16 row = 0;
        mouseProtocolCoordinates(tracking.enc, pixelX, pixelY, column, row);
        if (mouseContext.frontend.reportMotion(column, row, tracking.mode, tracking.enc, tracking.generation)) {
            mouseProtocolSend(tracking.enc, MouseEventType::Motion, modifiers, 0, column, row);
        }
    } else if (mouseContext.frontend.buttons() & ((1u << GLFW_MOUSE_BUTTON_LEFT) | (1u << GLFW_MOUSE_BUTTON_RIGHT))) {
        vt->selectUpdate(pixelX, pixelY);
    }
}

void ApplicationImpl::onMouseWheel(double wheelX, double wheelY) {
    const int modifiers = keyboardModifiers();
    const auto& tracking = vt->getMouseTrackingState();
    const bool reporting = isMouseProtocol(modifiers, tracking);
    const MouseWheelSteps steps = mouseContext.frontend.consumeWheel(wheelX, wheelY, reporting);
    if (reporting) {
        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(window, &x, &y);
        const int pixelX = toPixelX(x);
        const int pixelY = toPixelY(y);
        for (int k = 0; k < steps.y; ++k) {
            sendMouseButtonProtocol(MouseEventType::Press, 4, pixelX, pixelY, modifiers, tracking);
        }
        if (steps.y < 0) {
            for (int k = 0; k < -steps.y; ++k) {
                sendMouseButtonProtocol(MouseEventType::Press, 5, pixelX, pixelY, modifiers, tracking);
            }
        }
        for (int k = 0; k < -steps.x; ++k) {
            sendMouseButtonProtocol(MouseEventType::Press, 6, pixelX, pixelY, modifiers, tracking);
        }
        for (int k = 0; k < steps.x; ++k) {
            sendMouseButtonProtocol(MouseEventType::Press, 7, pixelX, pixelY, modifiers, tracking);
        }
    } else {
        if (steps.y > 0) {
            vt->mouseWheelUp(steps.y);
        } else if (steps.y < 0) {
            vt->mouseWheelDown(-steps.y);
        }
    }
}

std::string ApplicationImpl::getSelectionForOsc(bool primary) {
    return clipboardStore.get(primary);
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
            if (cwd.empty()) {
                logW << "OSC 7: cannot parse '" << argument << "'" << std::endl;
            } else if (!appTitleSet) {
                glfwSetWindowTitle(window, cwd.c_str());
            }
            return;
        }
        case 8:
            vt->setHyperlink(argument);
            return;
        case 133:
            return;
        case 52:
            break;
        default:
            logU << "unhandled OSC: '" << command << ';' << argument << "'" << std::endl;
            return;
    }

    const Osc52Request request = parseOsc52(argument, opts.osc52SelectClipboard);
    if (!request.valid) {
        logW << "Malformed OSC 52 argument" << std::endl;
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
        } else {
            logW << "OSC 52 clipboard read blocked; set "
                    "allowOsc52Read=true to enable"
                 << std::endl;
        }
        const std::string reply = encodeOsc52QueryReply(request, opts.allowOsc52Read, primary, clipboard);
        vt->writePty(reply.c_str());
        return;
    }

    clipboardStore.apply(request);
}

bool ApplicationImpl::present(const Frame& frame) {
    refreshPending = true;
    if (!refreshAllowed || !renderer->update(frame)) {
        return false;
    }
    refreshPending = false;
    return true;
}

void ApplicationImpl::osc(int command, const std::string& argument) {
    handleOsc(command, argument);
}

bool ApplicationImpl::handlesOsc() const {
    return terminalHostReady;
}

void ApplicationImpl::bell() {
    glfwRequestWindowAttention(window);
}

void ApplicationImpl::print(const std::string& output) {
    if (printerPipe == nullptr || output.empty()) {
        return;
    }
    const size_t written = fwrite(output.data(), 1, output.size(), printerPipe);
    if (written != output.size()) {
        logE << "Printer command stopped accepting output" << std::endl;
    }
    fflush(printerPipe);
}

void ApplicationImpl::leds(u8) {
}

void ApplicationImpl::notify(const std::string&, const std::string& title, const std::string& body, bool close) {
    if (close) {
        return;
    }
    logI << "Notification: " << title << ": " << body << std::endl;
    glfwRequestWindowAttention(window);
}

void ApplicationImpl::progress(u32 state, u32) {
    if (state == 2 || state == 4) {
        glfwRequestWindowAttention(window);
    }
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
        pixelHeight = 2 * opts.border + (int)(first)*fontpk->getPy();
        pixelWidth = 2 * opts.border + (int)(second)*fontpk->getPx();
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
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    info.pixelWidth = std::max(0, width);
    info.pixelHeight = std::max(0, height);
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

    const int snappedWidth = gridAlignedWindowSize(width, opts.border, fontpk->getPx(), xScale, windowWidth);
    const int snappedHeight = gridAlignedWindowSize(height, opts.border, fontpk->getPy(), yScale, windowHeight);
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
        if (vt == nullptr) {
            return;
        }
        if (!focused) {
            mouseContext.frontend.clearButtons();
            suppressedTextInputs = 0;
            pendingKittyTextKey.active = false;
            std::fill_n(locallyConsumedKeys, GLFW_KEY_LAST + 1, false);
        }
        vt->setHasFocus(focused == GLFW_TRUE);
    });
}

void ApplicationImpl::onKey(GLFWwindow*, int key, int scancode, int action, int modifiers) {
    guardCallback([this, key, scancode, action, modifiers]() {
        onKeyEvent(key, scancode, action, modifiers);
    });
}

void ApplicationImpl::onCharacter(GLFWwindow*, unsigned codepoint) {
    guardCallback([this, codepoint]() {
        onTextInput(codepoint);
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
        mouseContext.cursorInside = entered == GLFW_TRUE;
        mouseContext.frontend.resetMotion();
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
        ptySource.setWriteInterest(vt->hasPendingPtyOutput());
        refreshAllowed = false;
        double timeout = (vt->synchronizedOutputActive() || vt->animationActive()) ? 0.05 : -1.0;
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
        flushPendingKittyTextKey();
        if (glfwWindowShouldClose(window)) {
            return true;
        }
        vt->expireSynchronizedOutput();
        if (vt->advanceAnimation()) {
            windowContext.redrawPending = true;
        }
        bool resized = false;
        if (windowContext.resizePending) {
            const int width = std::min(windowContext.framebufferWidth, (int)(UINT16_MAX));
            const int height = std::min(windowContext.framebufferHeight, (int)(UINT16_MAX));
            windowContext.resizePending = false;
            windowContext.redrawPending = false;
            vt->resize(width, height);
            vt->redraw();
            committedRepaintPending = vt->synchronizedOutputActive();
            resized = true;
            refreshDeadline = Clock::now() + resizeGrace;
        } else if (windowContext.redrawPending) {
            windowContext.redrawPending = false;
            if (vt->synchronizedOutputActive()) {
                committedRepaintPending = true;
            } else {
                vt->redraw();
            }
        }
        const short ptyEvents = ptySource.events();
        const bool readPtyInput = (ptyEvents & (POLLIN | POLLHUP | POLLERR)) && !mouseContext.frontend.selectionOngoing();
        const bool finished = vt->servicePty(readPtyInput, ptyEvents & POLLOUT);
        if (readPtyInput) {
            ptySource.acknowledge();
            if (finished) {
                return false;
            }
        } else if (ptyEvents && !(ptyEvents & (POLLIN | POLLHUP | POLLERR))) {
            ptySource.acknowledge();
        }
        ptySource.setWriteInterest(vt->hasPendingPtyOutput());

        // A child responding to SIGWINCH is part of the same visual
        // update. Give it a short opportunity to redraw, then fall back
        // to the resized terminal state even if it produces no output.
        if (readPtyInput) {
            refreshDeadline.reset();
        }
        const auto now = Clock::now();
        if (!vt->synchronizedOutputActive()) {
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
            vt->redraw();
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
        std::cout << "Warning: could not set locale; international input "
                     "may be broken.\n";
        return;
    }
    if (std::strcmp(nl_langinfo(CODESET), "UTF-8") != 0) {
        std::cout << "Warning: non-UTF-8 locale " << locale << "; international input may be broken.\n";
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
    const int testFd = takeTestFd(argc, argv);
    checkLocale();
    opts.initialize(&argc, argv);
    opts.parse();
    if (opts.verbose) {
        opts.printVersion();
    }
    if (setenv("ZUTTY_VERSION", ZUTTY_VERSION, 1) < 0) {
        SYS_ERROR("setenv ZUTTY_VERSION");
    }
    if (testFd >= 0) {
        return runTestMode(composer, testFd, argc, argv);
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

    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
    if (!glfwInit()) {
        throw std::runtime_error(glfwFailure("glfwInit"));
    }
    glfwInitialized = true;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, "org.zutty.Zutty");

    const int initialWidth = std::max(320, (int)(opts.nCols) * opts.fontsize / 2);
    const int initialHeight = std::max(200, (int)(opts.nRows) * opts.fontsize);
    window = glfwCreateWindow(initialWidth, initialHeight, opts.title, nullptr, nullptr);
    if (window == nullptr) {
        throw std::runtime_error(glfwFailure("glfwCreateWindow"));
    }
    glfwSetWindowUserPointer(window, this);
    clipboardStore.setHandlers([this] {
        const char* text = glfwGetClipboardString(window);
        return text != nullptr ? std::string(text) : std::string{};
    }, [this](const std::string& text) {
        glfwSetClipboardString(window, text.c_str());
    });
    glfwSetInputMode(window, GLFW_LOCK_KEY_MODS, GLFW_TRUE);

    float xScale = 1.0f;
    float yScale = 1.0f;
    glfwGetWindowContentScale(window, &xScale, &yScale);
    const float density = std::max({1.0f, xScale, yScale});
    opts.fontsize = (u8)(std::clamp((int)(std::lround(opts.fontsize * density)), 1, 255));
    opts.border = (u16)(std::clamp((int)(std::lround(opts.border * density)), 0, 3000));

    fontpk = Fontpack::create(composer, opts.fontname, opts.dwfontname);
    composer.fonts = fontpk;
    const int desiredPixelWidth = 2 * opts.border + opts.nCols * fontpk->getPx();
    const int desiredPixelHeight = 2 * opts.border + opts.nRows * fontpk->getPy();
    const int desiredWidth = std::max(1, (int)(std::ceil(desiredPixelWidth / density)));
    const int desiredHeight = std::max(1, (int)(std::ceil(desiredPixelHeight / density)));
    glfwSetWindowSizeLimits(window, std::max(1, (int)(std::ceil((2 * opts.border + fontpk->getPx()) / density))), std::max(1, (int)(std::ceil((2 * opts.border + fontpk->getPy()) / density))), GLFW_DONT_CARE, GLFW_DONT_CARE);
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

    cursor = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    hyperlinkCursor = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
    if (cursor != nullptr) {
        glfwSetCursor(window, cursor);
    }

    renderer = Renderer::create(composer, window);
    composer.renderer = renderer;
    setupSignals();
    const int ptyFd = startShell(launch.executable.c_str(), shellArgv.data());
    Pty* terminalPty = Pty::adopt(composer, ptyFd);
    composer.pty = terminalPty;
    if (opts.printerCommand != nullptr && opts.printerCommand[0] != '\0') {
        printerPipe = popen(opts.printerCommand, "w");
        if (printerPipe == nullptr) {
            throw std::runtime_error("Cannot start printer command");
        }
    }
    vt = Vterm::create(composer, *this, *terminalPty, fontpk->getPx(), fontpk->getPy(), pixelWidth, pixelHeight);
    composer.vterm = vt;
    terminalHostReady = true;
    setupCallbacks();
    vt->setHasFocus(glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE);
    vt->resize(pixelWidth, pixelHeight);
    vt->redraw();

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
    renderer = nullptr;
    composer.renderer = nullptr;
    fontpk = nullptr;
    composer.fonts = nullptr;
    return 0;
}

void ApplicationImpl::emergencyCleanup() {
    vt = nullptr;
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
