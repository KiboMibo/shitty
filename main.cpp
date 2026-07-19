/* This file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "base.h"
#include "fontpack.h"
#include "log.h"
#include "mouseprotocol.h"
#include "oscprotocol.h"
#include "options.h"
#include "pty.h"
#include "renderer.h"
#include "testmode.h"
#include "utf8.h"
#include "vterm.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <langinfo.h>
#include <limits.h>
#include <memory>
#include <mutex>
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
#include <thread>
#include <unistd.h>
#include <vector>

static std::unique_ptr<Fontpack> fontpk;
static std::unique_ptr<Renderer> renderer;
static std::unique_ptr<Vterm> vt;
static GLFWwindow* window = nullptr;
static GLFWcursor* cursor = nullptr;
static GLFWcursor* hyperlinkCursor = nullptr;
static bool glfwInitialized = false;

extern char** environ;

namespace {
    int takeTestFd(int& argc, char* argv[]) {
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
            return static_cast<int>(fd);
        }
        return -1;
    }

    class PtyEventSource {
    public:
        explicit PtyEventSource(int ptyFd_)
            : ptyFd(ptyFd_)
        {
            if (pipe(wakePipe) < 0) {
                throw std::runtime_error(
                    std::string("pipe failed: ") + std::strerror(errno));
            }
            for (const int fd : wakePipe) {
                const int flags = fcntl(fd, F_GETFD);
                if (flags >= 0) {
                    fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
                }
            }
            worker = std::thread(&PtyEventSource::run, this);
        }

        ~PtyEventSource() {
            {
                std::lock_guard<std::mutex> lock(mutex);
                stopping = true;
                pending = false;
            }
            condition.notify_all();
            const uint8_t byte = 1;
            while (write(wakePipe[1], &byte, sizeof(byte)) < 0 &&
                   errno == EINTR)
                ;
            if (worker.joinable()) {
                worker.join();
            }
            close(wakePipe[0]);
            close(wakePipe[1]);
        }

        void acknowledge() {
            {
                std::lock_guard<std::mutex> lock(mutex);
                pending = false;
            }
            condition.notify_one();
        }

        bool isPending() {
            std::lock_guard<std::mutex> lock(mutex);
            return pending;
        }

    private:
        int ptyFd;
        int wakePipe[2]{-1, -1};
        std::thread worker;
        std::mutex mutex;
        std::condition_variable condition;
        bool stopping = false;
        bool pending = false;

        void run() {
            struct pollfd pollSet[] = {
                {ptyFd, POLLIN | POLLHUP, 0},
                {wakePipe[0], POLLIN, 0},
            };

            while (true) {
                const int result = poll(pollSet, 2, -1);
                if (result < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    return;
                }
                if (pollSet[1].revents & POLLIN) {
                    return;
                }
                if (!(pollSet[0].revents & (POLLIN | POLLHUP | POLLERR))) {
                    continue;
                }

                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (stopping) {
                        return;
                    }
                    pending = true;
                }

                glfwPostEmptyEvent();

                std::unique_lock<std::mutex> lock(mutex);
                condition.wait(lock, [this] {
                    return stopping || !pending;
                });
                if (stopping) {
                    return;
                }
            }
        }
    };

    struct MouseContext {
        bool selectionOngoing = false;
        bool hyperlinkClick = false;
        bool scrollReporting = false;
        unsigned buttonState = 0;
        int lastButton = -1;
        int clickCount = 0;
        double lastClickTime = 0.0;
        double lastClickX = 0.0;
        double lastClickY = 0.0;
        double scrollRemainderX = 0.0;
        double scrollRemainderY = 0.0;
    } mouseContext;

    struct WindowContext {
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        bool resizePending = false;
        bool redrawPending = false;
        bool correctingResize = false;
    } windowContext;

    unsigned suppressedTextInputs = 0;
    bool locallyConsumedKeys[GLFW_KEY_LAST + 1]{};

    int gridAlignedWindowSize(int framebufferSize, int border,
                              int cellSize, float scale,
                              int currentWindowSize) {
        const int innerSize = framebufferSize - 2 * border;
        if (innerSize < cellSize || scale <= 0.0f) {
            return currentWindowSize;
        }

        // Wayland fractional scale is expressed in units of 1/120. Find the
        // largest cell-aligned framebuffer size representable by an integer
        // logical window size.
        const int scaleNumerator = std::max(
            120, static_cast<int>(std::lround(scale * 120.0f)));
        for (int cells = innerSize / cellSize; cells > 0; --cells) {
            const int framebufferTarget = 2 * border + cells * cellSize;
            const int windowTarget = static_cast<int>(
                (static_cast<int64_t>(framebufferTarget) * 120 +
                 scaleNumerator - 1) /
                scaleNumerator);
            if (static_cast<int64_t>(windowTarget) * scaleNumerator / 120 ==
                framebufferTarget) {
                return windowTarget;
            }
        }
        return currentWindowSize;
    }

    void queueFramebufferResize(int width, int height) {
        windowContext.framebufferWidth = width;
        windowContext.framebufferHeight = height;
        windowContext.resizePending = true;
    }

    std::string primarySelection;
    std::exception_ptr callbackError;

    void resolveShell(char* progPath) {
        char resolvedPath[PATH_MAX];
        if (progPath[0] == '/') {
            return;
        }
        if (progPath[0] == '.' &&
            realpath(progPath, resolvedPath) != nullptr) {
            std::strncpy(progPath, resolvedPath, PATH_MAX - 1);
            progPath[PATH_MAX - 1] = '\0';
            return;
        }

        const char* pathValue = getenv("PATH");
        char* path = pathValue != nullptr ? strdup(pathValue) : nullptr;
        if (path != nullptr) {
            char testPath[PATH_MAX + 1];
            char* part = std::strtok(path, ":");
            while (part != nullptr) {
                std::snprintf(testPath, sizeof(testPath), "%s/%s",
                              part, progPath);
                if (realpath(testPath, resolvedPath) != nullptr) {
                    std::strncpy(progPath, resolvedPath, PATH_MAX - 1);
                    progPath[PATH_MAX - 1] = '\0';
                    free(path);
                    return;
                }
                part = std::strtok(nullptr, ":");
            }
            free(path);
        }

        const char* shell = getenv("SHELL");
        struct stat statbuf{};
        if (shell != nullptr && stat(shell, &statbuf) == 0 &&
            (statbuf.st_mode & S_IXUSR)) {
            std::strncpy(progPath, shell, PATH_MAX - 1);
            progPath[PATH_MAX - 1] = '\0';
            return;
        }

        const passwd* entry = getpwuid(getuid());
        shell = entry != nullptr ? entry->pw_shell : nullptr;
        if (shell != nullptr && stat(shell, &statbuf) == 0 &&
            (statbuf.st_mode & S_IXUSR)) {
            std::strncpy(progPath, shell, PATH_MAX - 1);
            progPath[PATH_MAX - 1] = '\0';
            return;
        }
        std::strcpy(progPath, "/bin/sh");
    }

    void validateShell(char* progPath) {
        resolveShell(progPath);
        for (char* permitted = getusershell(); permitted != nullptr;
             permitted = getusershell()) {
            if (std::strcmp(progPath, permitted) == 0) {
                endusershell();
                setenv("SHELL", progPath, 1);
                return;
            }
        }
        endusershell();
        unsetenv("SHELL");
    }

    void setArgv0(char* argv0) {
        const char* basename = std::strrchr(opts.shell, '/');
        basename = basename != nullptr ? basename + 1 : opts.shell;
        if (opts.login) {
            argv0[0] = '-';
            std::strncpy(argv0 + 1, basename, PATH_MAX - 2);
            argv0[PATH_MAX - 1] = '\0';
        } else {
            std::strncpy(argv0, basename, PATH_MAX - 1);
            argv0[PATH_MAX - 1] = '\0';
        }
    }

    void childSignalHandler(int signal, siginfo_t* info, void*) {
        if (signal == SIGCHLD && info != nullptr) {
            waitpid(info->si_pid, nullptr, WNOHANG);
        }
    }

    void setupSignals() {
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

    int startShell(const char* execPath, const char* const argv[]) {
        int ptyFd = -1;
        const pid_t pid = pty_fork(ptyFd, opts.nCols, opts.nRows);
        if (pid < 0) {
            SYS_ERROR("fork");
        }
        if (pid == 0) {
            if (setenv("TERM", "xterm-256color", 1) < 0) {
                SYS_ERROR("setenv TERM");
            }
            if (execvp(execPath, const_cast<char* const*>(argv)) < 0) {
                SYS_ERROR("execvp of ", execPath);
            }
        }
        logT << "Shell subprocess started, pid: " << pid << std::endl;
        return ptyFd;
    }

    bool keyPressed(int key) {
        return glfwGetKey(window, key) == GLFW_PRESS;
    }

    int keyboardModifiers() {
        int modifiers = 0;
        if (keyPressed(GLFW_KEY_LEFT_SHIFT) ||
            keyPressed(GLFW_KEY_RIGHT_SHIFT)) {
            modifiers |= GLFW_MOD_SHIFT;
        }
        if (keyPressed(GLFW_KEY_LEFT_CONTROL) ||
            keyPressed(GLFW_KEY_RIGHT_CONTROL)) {
            modifiers |= GLFW_MOD_CONTROL;
        }
        if (keyPressed(GLFW_KEY_LEFT_ALT)) {
            modifiers |= GLFW_MOD_ALT;
        }
        return modifiers;
    }

    VtModifier convertModifiers(int modifiers) {
        VtModifier result = VtModifier::none;
        if (modifiers & GLFW_MOD_SHIFT) {
            result = result | VtModifier::shift;
        }
        if (modifiers & GLFW_MOD_CONTROL) {
            result = result | VtModifier::control;
        }
        if ((modifiers & GLFW_MOD_ALT) &&
            !keyPressed(GLFW_KEY_RIGHT_ALT)) {
            result = result | VtModifier::alt;
        }
        return result;
    }

    int significantModifiers(int modifiers) {
        return modifiers & (GLFW_MOD_SHIFT | GLFW_MOD_CONTROL | GLFW_MOD_ALT);
    }

    uint16_t kittyModifiers(int modifiers) {
        uint16_t result = 0;
        if (modifiers & GLFW_MOD_SHIFT) {
            result |= 1;
        }
        if ((modifiers & GLFW_MOD_ALT) &&
            !keyPressed(GLFW_KEY_RIGHT_ALT)) {
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

    uint32_t decodeKeyName(const char* name) {
        if (name == nullptr || !*name) {
            return 0;
        }
        const auto* bytes = reinterpret_cast<const unsigned char*>(name);
        if (bytes[0] < 0x80) {
            return bytes[0];
        }
        if ((bytes[0] & 0xe0) == 0xc0 && bytes[1]) {
            return ((bytes[0] & 0x1f) << 6) | (bytes[1] & 0x3f);
        }
        if ((bytes[0] & 0xf0) == 0xe0 && bytes[1] && bytes[2]) {
            return ((bytes[0] & 0x0f) << 12) |
                   ((bytes[1] & 0x3f) << 6) | (bytes[2] & 0x3f);
        }
        if ((bytes[0] & 0xf8) == 0xf0 &&
            bytes[1] && bytes[2] && bytes[3]) {
            return ((bytes[0] & 0x07) << 18) |
                   ((bytes[1] & 0x3f) << 12) |
                   ((bytes[2] & 0x3f) << 6) | (bytes[3] & 0x3f);
        }
        return 0;
    }

    uint32_t baseLayoutKey(int key) {
        if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
            return key - GLFW_KEY_A + 'a';
        }
        if ((key >= GLFW_KEY_0 && key <= GLFW_KEY_9) ||
            key == GLFW_KEY_SPACE || key == GLFW_KEY_APOSTROPHE ||
            key == GLFW_KEY_COMMA || key == GLFW_KEY_MINUS ||
            key == GLFW_KEY_PERIOD || key == GLFW_KEY_SLASH ||
            key == GLFW_KEY_SEMICOLON || key == GLFW_KEY_EQUAL ||
            key == GLFW_KEY_LEFT_BRACKET || key == GLFW_KEY_BACKSLASH ||
            key == GLFW_KEY_RIGHT_BRACKET || key == GLFW_KEY_GRAVE_ACCENT) {
            return key;
        }
        return 0;
    }

    uint32_t shiftedKey(uint32_t key) {
        if (key >= 'a' && key <= 'z') {
            return key - 'a' + 'A';
        }
        switch (key) {
            case '`':
                return '~';
            case '1':
                return '!';
            case '2':
                return '@';
            case '3':
                return '#';
            case '4':
                return '$';
            case '5':
                return '%';
            case '6':
                return '^';
            case '7':
                return '&';
            case '8':
                return '*';
            case '9':
                return '(';
            case '0':
                return ')';
            case '-':
                return '_';
            case '=':
                return '+';
            case '[':
                return '{';
            case ']':
                return '}';
            case '\\':
                return '|';
            case ';':
                return ':';
            case '\'':
                return '"';
            case ',':
                return '<';
            case '.':
                return '>';
            case '/':
                return '?';
            default:
                return 0;
        }
    }

    bool pasteSelection(bool primary) {
        const char* text = nullptr;
        if (primary) {
            text = primarySelection.c_str();
        } else {
            text = glfwGetClipboardString(window);
        }
        if (text == nullptr) {
            return false;
        }
        vt->pasteSelection(text);
        return true;
    }

    bool copyPrimaryToClipboard() {
        if (primarySelection.empty()) {
            return false;
        }
        glfwSetClipboardString(window, primarySelection.c_str());
        return true;
    }

    VtKey keypadKey(int key, bool numLock) {
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

    VtKey specialKey(int key, int modifiers) {
        using Key = VtKey;
        const Key keypad = keypadKey(
            key, (modifiers & GLFW_MOD_NUM_LOCK) != 0);
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
            default:
                return Key::NONE;
        }
    }

    bool controlCharacter(int key, int modifiers, uint8_t& character) {
        if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
            character = static_cast<uint8_t>(key - GLFW_KEY_A + 1);
            return true;
        }
        switch (key) {
            case GLFW_KEY_SPACE:
            case GLFW_KEY_2:
                character = 0;
                return true;
            case GLFW_KEY_3:
            case GLFW_KEY_LEFT_BRACKET:
                character = 27;
                return true;
            case GLFW_KEY_4:
            case GLFW_KEY_BACKSLASH:
                character = 28;
                return true;
            case GLFW_KEY_5:
            case GLFW_KEY_RIGHT_BRACKET:
                character = 29;
                return true;
            case GLFW_KEY_6:
                character = 30;
                return true;
            case GLFW_KEY_7:
                character = 31;
                return true;
            case GLFW_KEY_8:
                character = 127;
                return true;
            case GLFW_KEY_MINUS:
                if (!(modifiers & GLFW_MOD_SHIFT)) {
                    character = static_cast<uint8_t>(key);
                    return true;
                }
                character = 31;
                return true;
            case GLFW_KEY_SLASH:
                character = modifiers & GLFW_MOD_SHIFT ? 127 : 31;
                return true;
            default:
                if (key > 0 && key < 128) {
                    character = static_cast<uint8_t>(key);
                    return true;
                }
                return false;
        }
    }

    void onKeyEvent(int key, int scancode, int action, int rawModifiers) {
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
        if ((key == GLFW_KEY_INSERT || key == GLFW_KEY_KP_0) &&
            modifiers == VtModifier::shift) {
            runLocal([&]() {
                pasteSelection(true);
            });
            return;
        }
        if (key == GLFW_KEY_SPACE && mouseContext.selectionOngoing) {
            runLocal([&]() {
                vt->selectRectangularModeToggle();
            });
            return;
        }

        const uint8_t kittyFlags = vt->getKittyKeyboardFlags();
        const uint16_t kittyMods = kittyModifiers(rawModifiers);
        const auto event = action == GLFW_RELEASE
                               ? Vterm::KeyEventType::Release
                           : action == GLFW_REPEAT
                               ? Vterm::KeyEventType::Repeat
                               : Vterm::KeyEventType::Press;

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

            const uint32_t baseKey = baseLayoutKey(key);
            uint32_t primaryKey = decodeKeyName(
                glfwGetKeyName(key, scancode));
            if (!primaryKey) {
                primaryKey = baseKey;
            }
            const uint16_t textMods = kittyMods & ~(64 | 128);
            if (primaryKey && (textMods & (2 | 4 | 8))) {
                const uint32_t alternate = textMods & 1
                                               ? shiftedKey(primaryKey)
                                               : 0;
                vt->writeKittyKey(primaryKey, alternate, baseKey,
                                  textMods, event);
                if (pressed && (textMods & (2 | 8)) &&
                    !(textMods & 4)) {
                    ++suppressedTextInputs;
                }
                return;
            }
        }

        if (!pressed) {
            return;
        }

        if (key == GLFW_KEY_ESCAPE) {
            vt->writePty(static_cast<uint8_t>('\x1b'), modifiers, true);
            return;
        }

        const VtKey special = specialKey(key, keyModifiers);
        if (special != VtKey::NONE) {
            vt->writePty(special, modifiers, true);
            return;
        }

        if (legacyModifiers & GLFW_MOD_CONTROL) {
            uint8_t character = 0;
            if (controlCharacter(key, legacyModifiers, character)) {
                vt->writePty(character, modifiers, true);
            }
        }
    }

    void onTextInput(uint32_t codepoint) {
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
            vt->writePty(static_cast<uint8_t>(codepoint), modifiers, true);
            return;
        }

        std::string text;
        Utf8Encoder::pushUnicode(
            codepoint, [&text](uint8_t byte) {
            text.push_back(static_cast<char>(byte));
        });
        if ((rawModifiers & GLFW_MOD_ALT) && opts.altSendsEscape) {
            vt->writePty("\x1b", true);
        }
        vt->writePty(text.c_str(), true);
    }

    double pixelScaleX() {
        int windowWidth = 0;
        int framebufferWidth = 0;
        glfwGetWindowSize(window, &windowWidth, nullptr);
        glfwGetFramebufferSize(window, &framebufferWidth, nullptr);
        if (windowWidth <= 0) {
            return 1.0;
        }
        return std::max(
            1.0, static_cast<double>(framebufferWidth) / windowWidth);
    }

    double pixelScaleY() {
        int windowHeight = 0;
        int framebufferHeight = 0;
        glfwGetWindowSize(window, nullptr, &windowHeight);
        glfwGetFramebufferSize(window, nullptr, &framebufferHeight);
        if (windowHeight <= 0) {
            return 1.0;
        }
        return std::max(
            1.0, static_cast<double>(framebufferHeight) / windowHeight);
    }

    int toPixelX(double x) {
        return static_cast<int>(std::lround(x * pixelScaleX()));
    }

    int toPixelY(double y) {
        return static_cast<int>(std::lround(y * pixelScaleY()));
    }

    bool isMouseProtocol(int modifiers,
                         const MouseTrackingState& tracking) {
        return !mouseContext.selectionOngoing &&
               !(modifiers & GLFW_MOD_SHIFT) &&
               tracking.mode != MouseTrackingMode::Disabled;
    }

    void mouseProtocolCoordinates(int pixelX, int pixelY,
                                  uint16_t& column, uint16_t& row) {
        column = std::max(0, (pixelX - opts.border - 1) /
                                 fontpk->getPx()) +
                 1;
        row = std::max(0, (pixelY - opts.border - 1) /
                              fontpk->getPy()) +
              1;
    }

    void mouseProtocolSend(MouseTrackingEnc encoding, MouseEventType type,
                           int modifiers, unsigned buttonState,
                           int button, int column, int row) {
        unsigned protocolModifiers = 0;
        if (modifiers & GLFW_MOD_SHIFT) {
            protocolModifiers |= MouseShift;
        }
        if ((modifiers & GLFW_MOD_ALT) &&
            !keyPressed(GLFW_KEY_RIGHT_ALT)) {
            protocolModifiers |= MouseAlt;
        }
        if (modifiers & GLFW_MOD_CONTROL) {
            protocolModifiers |= MouseControl;
        }
        int motionButton = 0;
        if (buttonState & (1u << GLFW_MOUSE_BUTTON_LEFT)) {
            motionButton = 1;
        } else if (buttonState & (1u << GLFW_MOUSE_BUTTON_MIDDLE)) {
            motionButton = 2;
        } else if (buttonState & (1u << GLFW_MOUSE_BUTTON_RIGHT)) {
            motionButton = 3;
        }
        vt->writePty(encodeMouseProtocol(
            encoding, type, protocolModifiers, motionButton,
            button, column, row).c_str());
    }

    void sendMouseButtonProtocol(MouseEventType type, int button,
                                 int pixelX, int pixelY,
                                 int modifiers, unsigned buttonState,
                                 const MouseTrackingState& tracking) {
        if (button > 11 ||
            (type == MouseEventType::Release && button > 3)) {
            return;
        }
        if (tracking.mode == MouseTrackingMode::Disabled ||
            (type == MouseEventType::Release &&
             tracking.mode == MouseTrackingMode::X10_Compat)) {
            return;
        }

        uint16_t column = 0;
        uint16_t row = 0;
        mouseProtocolCoordinates(pixelX, pixelY, column, row);
        const int protocolModifiers =
            tracking.mode == MouseTrackingMode::X10_Compat ? 0 : modifiers;
        mouseProtocolSend(
            tracking.enc, type, protocolModifiers,
            buttonState, button, column, row);
    }

    int terminalButton(int button) {
        switch (button) {
            case GLFW_MOUSE_BUTTON_LEFT:
                return 1;
            case GLFW_MOUSE_BUTTON_MIDDLE:
                return 2;
            case GLFW_MOUSE_BUTTON_RIGHT:
                return 3;
            default:
                if (button < GLFW_MOUSE_BUTTON_4) {
                    return 0;
                }
                return button - GLFW_MOUSE_BUTTON_4 + 8;
        }
    }

    bool isMultipleClick(int button, double x, double y) {
        const double now = glfwGetTime();
        const bool repeated = button == mouseContext.lastButton &&
                              now - mouseContext.lastClickTime <= 0.5 &&
                              std::abs(x - mouseContext.lastClickX) <= 4.0 &&
                              std::abs(y - mouseContext.lastClickY) <= 4.0;
        mouseContext.clickCount = repeated ? mouseContext.clickCount + 1 : 1;
        mouseContext.lastButton = button;
        mouseContext.lastClickTime = now;
        mouseContext.lastClickX = x;
        mouseContext.lastClickY = y;
        return mouseContext.clickCount > 1;
    }

    void openHyperlink(const std::string& uri) {
        pid_t pid = -1;
        char* const argv[] = {
            const_cast<char*>("xdg-open"),
            const_cast<char*>(uri.c_str()),
            nullptr,
        };
        const int error = posix_spawnp(
            &pid, argv[0], nullptr, nullptr, argv, environ);
        if (error != 0) {
            logW << "Cannot open hyperlink '" << uri
                 << "': " << strerror(error) << std::endl;
        }
    }

    void onMouseButton(int button, bool pressed, int modifiers) {
        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(window, &x, &y);
        const int pixelX = toPixelX(x);
        const int pixelY = toPixelY(y);
        const unsigned buttonMask = 1u << button;
        if (pressed) {
            mouseContext.buttonState |= buttonMask;
        } else {
            mouseContext.buttonState &= ~buttonMask;
        }
        const auto& tracking = vt->getMouseTrackingState();
        const int protocolButton = terminalButton(button);

        if (!pressed && button == GLFW_MOUSE_BUTTON_LEFT &&
            mouseContext.hyperlinkClick) {
            mouseContext.hyperlinkClick = false;
            return;
        }
        if (pressed && button == GLFW_MOUSE_BUTTON_LEFT &&
            (modifiers & GLFW_MOD_CONTROL)) {
            const std::string uri = vt->getHyperlink(pixelX, pixelY);
            if (!uri.empty()) {
                mouseContext.hyperlinkClick = true;
                openHyperlink(uri);
                return;
            }
        }

        if (isMouseProtocol(modifiers, tracking)) {
            sendMouseButtonProtocol(
                pressed ? MouseEventType::Press : MouseEventType::Release,
                protocolButton, pixelX, pixelY, modifiers,
                mouseContext.buttonState, tracking);
            return;
        }

        if (pressed) {
            const bool cycleSnapTo = isMultipleClick(button, x, y);
            if (button == GLFW_MOUSE_BUTTON_LEFT) {
                vt->selectStart(pixelX, pixelY, cycleSnapTo);
                mouseContext.selectionOngoing = true;
            } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
                vt->selectExtend(pixelX, pixelY, cycleSnapTo);
                mouseContext.selectionOngoing = true;
            }
            return;
        }

        if (button == GLFW_MOUSE_BUTTON_LEFT ||
            button == GLFW_MOUSE_BUTTON_RIGHT) {
            std::string selection;
            mouseContext.selectionOngoing = false;
            if (vt->selectFinish(selection)) {
                primarySelection = selection;
                if (opts.autoCopyMode) {
                    glfwSetClipboardString(window, selection.c_str());
                }
            }
        } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
            pasteSelection(true);
        }
    }

    void onMouseMotion(double x, double y) {
        const int pixelX = toPixelX(x);
        const int pixelY = toPixelY(y);
        const int modifiers = keyboardModifiers();
        const bool overHyperlink = (modifiers & GLFW_MOD_CONTROL) &&
                                   !vt->getHyperlink(pixelX, pixelY).empty();
        glfwSetCursor(window, overHyperlink && hyperlinkCursor != nullptr
                                  ? hyperlinkCursor
                                  : cursor);
        const auto& tracking = vt->getMouseTrackingState();
        if (isMouseProtocol(modifiers, tracking)) {
            if (tracking.mode == MouseTrackingMode::VT200_ButtonEvent &&
                !(mouseContext.buttonState &
                  ((1u << GLFW_MOUSE_BUTTON_LEFT) |
                   (1u << GLFW_MOUSE_BUTTON_MIDDLE) |
                   (1u << GLFW_MOUSE_BUTTON_RIGHT)))) {
                return;
            }
            if (tracking.mode != MouseTrackingMode::VT200_ButtonEvent &&
                tracking.mode != MouseTrackingMode::VT200_AnyEvent) {
                return;
            }

            static uint16_t lastColumn = UINT16_MAX;
            static uint16_t lastRow = UINT16_MAX;
            uint16_t column = 0;
            uint16_t row = 0;
            mouseProtocolCoordinates(pixelX, pixelY, column, row);
            if (column != lastColumn || row != lastRow) {
                mouseProtocolSend(tracking.enc, MouseEventType::Motion,
                                  modifiers, mouseContext.buttonState,
                                  0, column, row);
                lastColumn = column;
                lastRow = row;
            }
        } else if (mouseContext.buttonState &
                   ((1u << GLFW_MOUSE_BUTTON_LEFT) |
                    (1u << GLFW_MOUSE_BUTTON_RIGHT))) {
            vt->selectUpdate(pixelX, pixelY);
        }
    }

    void onMouseWheel(double wheelX, double wheelY) {
        const int modifiers = keyboardModifiers();
        const auto& tracking = vt->getMouseTrackingState();
        const bool reporting = isMouseProtocol(modifiers, tracking);
        if (reporting != mouseContext.scrollReporting) {
            mouseContext.scrollReporting = reporting;
            mouseContext.scrollRemainderX = 0.0;
            mouseContext.scrollRemainderY = 0.0;
        }

        auto consumeDelta = [](double delta, double& remainder) {
            if (!std::isfinite(delta)) {
                remainder = 0.0;
                return 0;
            }
            const double total = remainder +
                                 std::clamp(delta, -100.0, 100.0);
            const int steps = static_cast<int>(std::trunc(total));
            remainder = total - steps;
            return steps;
        };

        const int stepsY = consumeDelta(
            wheelY, mouseContext.scrollRemainderY);
        if (reporting) {
            const int stepsX = consumeDelta(
                wheelX, mouseContext.scrollRemainderX);
            double x = 0.0;
            double y = 0.0;
            glfwGetCursorPos(window, &x, &y);
            const int pixelX = toPixelX(x);
            const int pixelY = toPixelY(y);
            for (int k = 0; k < stepsY; ++k) {
                sendMouseButtonProtocol(MouseEventType::Press, 4,
                                        pixelX, pixelY, modifiers,
                                        mouseContext.buttonState, tracking);
            }
            if (stepsY < 0) {
                for (int k = 0; k < -stepsY; ++k) {
                    sendMouseButtonProtocol(MouseEventType::Press, 5,
                                            pixelX, pixelY, modifiers,
                                            mouseContext.buttonState,
                                            tracking);
                }
            }
            for (int k = 0; k < -stepsX; ++k) {
                sendMouseButtonProtocol(MouseEventType::Press, 6,
                                        pixelX, pixelY, modifiers,
                                        mouseContext.buttonState, tracking);
            }
            for (int k = 0; k < stepsX; ++k) {
                sendMouseButtonProtocol(MouseEventType::Press, 7,
                                        pixelX, pixelY, modifiers,
                                        mouseContext.buttonState, tracking);
            }
        } else {
            mouseContext.scrollRemainderX = 0.0;
            if (stepsY > 0) {
                vt->mouseWheelUp(stepsY);
            } else if (stepsY < 0) {
                vt->mouseWheelDown(-stepsY);
            }
        }
    }

    std::string getSelectionForOsc(bool primary) {
        if (primary) {
            return primarySelection;
        }
        const char* text = glfwGetClipboardString(window);
        if (text == nullptr) {
            return {};
        }
        return text;
    }

    bool appTitleSet = false;

    void handleOsc(int command, const std::string& argument) {
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
            case 52:
                break;
            default:
                logU << "unhandled OSC: '" << command << ';' << argument << "'"
                     << std::endl;
                return;
        }

        const Osc52Request request = parseOsc52(
            argument, opts.osc52SelectClipboard);
        if (!request.valid) {
            logW << "Malformed OSC 52 argument" << std::endl;
            return;
        }

        if (request.query) {
            std::string content;
            if (opts.allowOsc52Read) {
                if (request.primary) {
                    content = getSelectionForOsc(true);
                }
                if (content.empty() && request.clipboard) {
                    content = getSelectionForOsc(false);
                }
            } else {
                logW << "OSC 52 clipboard read blocked; set "
                        "allowOsc52Read=true to enable" << std::endl;
            }
            const std::string reply = encodeOsc52Reply(
                request.replySelector, content);
            vt->writePty(reply.c_str());
            return;
        }

        if (request.primary) {
            primarySelection = request.content;
        }
        if (request.clipboard) {
            glfwSetClipboardString(window, request.content.c_str());
        }
    }

    template <typename Fn>
    void guardCallback(Fn&& callback) {
        if (callbackError != nullptr) {
            return;
        }
        try {
            callback();
        } catch (...) {
            callbackError = std::current_exception();
        }
    }

    void onFramebufferSize(GLFWwindow*, int width, int height) {
        if (width <= 0 || height <= 0) {
            return;
        }
        if (windowContext.correctingResize || fontpk == nullptr ||
            glfwGetWindowMonitor(window) != nullptr ||
            glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE) {
            queueFramebufferResize(width, height);
            return;
        }

        int windowWidth = 0;
        int windowHeight = 0;
        glfwGetWindowSize(window, &windowWidth, &windowHeight);
        float xScale = 1.0f;
        float yScale = 1.0f;
        glfwGetWindowContentScale(window, &xScale, &yScale);

        const int snappedWidth = gridAlignedWindowSize(
            width, opts.border, fontpk->getPx(), xScale, windowWidth);
        const int snappedHeight = gridAlignedWindowSize(
            height, opts.border, fontpk->getPy(), yScale, windowHeight);
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

    void onWindowRefresh(GLFWwindow*) {
        windowContext.redrawPending = true;
    }

    void onWindowFocus(GLFWwindow*, int focused) {
        guardCallback(
            [focused]() {
            if (vt == nullptr) {
                return;
            }
            if (!focused) {
                mouseContext.buttonState = 0;
                suppressedTextInputs = 0;
                std::fill_n(locallyConsumedKeys, GLFW_KEY_LAST + 1, false);
            }
            vt->setHasFocus(focused == GLFW_TRUE);
        });
    }

    void onKey(GLFWwindow*, int key, int scancode,
               int action, int modifiers) {
        guardCallback([key, scancode, action, modifiers]() {
            onKeyEvent(key, scancode, action, modifiers);
        });
    }

    void onCharacter(GLFWwindow*, unsigned codepoint) {
        guardCallback([codepoint]() {
            onTextInput(codepoint);
        });
    }

    void onMouseButtonCallback(GLFWwindow*, int button, int action,
                               int modifiers) {
        guardCallback([button, action, modifiers]() {
            onMouseButton(button, action == GLFW_PRESS, modifiers);
        });
    }

    void onCursorPosition(GLFWwindow*, double x, double y) {
        guardCallback([x, y]() {
            onMouseMotion(x, y);
        });
    }

    void onScroll(GLFWwindow*, double x, double y) {
        guardCallback([x, y]() {
            onMouseWheel(x, y);
        });
    }

    void setupCallbacks() {
        glfwSetFramebufferSizeCallback(window, onFramebufferSize);
        glfwSetWindowRefreshCallback(window, onWindowRefresh);
        glfwSetWindowFocusCallback(window, onWindowFocus);
        glfwSetKeyCallback(window, onKey);
        glfwSetCharCallback(window, onCharacter);
        glfwSetMouseButtonCallback(window, onMouseButtonCallback);
        glfwSetCursorPosCallback(window, onCursorPosition);
        glfwSetScrollCallback(window, onScroll);
    }

    bool eventLoop(PtyEventSource& ptySource) {
        while (!glfwWindowShouldClose(window)) {
            glfwWaitEvents();
            if (callbackError != nullptr) {
                std::rethrow_exception(callbackError);
            }
            if (glfwWindowShouldClose(window)) {
                return true;
            }
            if (windowContext.resizePending) {
                const int width = std::min(
                    windowContext.framebufferWidth,
                    static_cast<int>(UINT16_MAX));
                const int height = std::min(
                    windowContext.framebufferHeight,
                    static_cast<int>(UINT16_MAX));
                windowContext.resizePending = false;
                windowContext.redrawPending = false;
                vt->resize(width, height);
                vt->redraw();
            } else if (windowContext.redrawPending) {
                windowContext.redrawPending = false;
                vt->redraw();
            }
            if (ptySource.isPending() &&
                !mouseContext.selectionOngoing) {
                const bool finished = vt->readPty();
                ptySource.acknowledge();
                if (finished) {
                    return false;
                }
            }
        }
        return true;
    }

    void checkLocale() {
        const char* locale = setlocale(LC_ALL, "");
        if (locale == nullptr) {
            std::cout << "Warning: could not set locale; international input "
                         "may be broken.\n";
            return;
        }
        if (std::strcmp(nl_langinfo(CODESET), "UTF-8") != 0) {
            std::cout << "Warning: non-UTF-8 locale " << locale
                      << "; international input may be broken.\n";
        }
    }

    std::string glfwFailure(const char* operation) {
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

    int run(int argc, char* argv[]) {
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
            return runTestMode(testFd);
        }

        char argv0[PATH_MAX]{};
        char progPath[PATH_MAX]{};
        char* defaultShellArgv[] = {argv0, nullptr};
        char** shellArgv = defaultShellArgv;
        if (argc > 2 && std::strcmp(argv[1], "-e") == 0) {
            shellArgv = argv + 2;
            if (opts.titleSource != OptionSource::CmdLine) {
                opts.title = argv[2];
            }
            std::strncpy(progPath, argv[2], PATH_MAX - 1);
        } else if (argc == 2) {
            setArgv0(argv0);
            std::strncpy(progPath, argv[1], PATH_MAX - 1);
            validateShell(progPath);
        } else {
            setArgv0(argv0);
            std::strncpy(progPath, opts.shell, PATH_MAX - 1);
            validateShell(progPath);
        }

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

        const int initialWidth = std::max(
            320, static_cast<int>(opts.nCols) * opts.fontsize / 2);
        const int initialHeight = std::max(
            200, static_cast<int>(opts.nRows) * opts.fontsize);
        window = glfwCreateWindow(
            initialWidth, initialHeight, opts.title, nullptr, nullptr);
        if (window == nullptr) {
            throw std::runtime_error(glfwFailure("glfwCreateWindow"));
        }
        glfwSetInputMode(window, GLFW_LOCK_KEY_MODS, GLFW_TRUE);

        float xScale = 1.0f;
        float yScale = 1.0f;
        glfwGetWindowContentScale(window, &xScale, &yScale);
        const float density = std::max({1.0f, xScale, yScale});
        opts.fontsize = static_cast<uint8_t>(std::clamp(
            static_cast<int>(std::lround(opts.fontsize * density)), 1, 255));
        opts.border = static_cast<uint16_t>(std::clamp(
            static_cast<int>(std::lround(opts.border * density)), 0, 3000));

        fontpk = std::make_unique<Fontpack>(
            opts.fontpath, opts.fontname, opts.dwfontname);
        const int desiredPixelWidth =
            2 * opts.border + opts.nCols * fontpk->getPx();
        const int desiredPixelHeight =
            2 * opts.border + opts.nRows * fontpk->getPy();
        const int desiredWidth = std::max(
            1, static_cast<int>(std::ceil(desiredPixelWidth / density)));
        const int desiredHeight = std::max(
            1, static_cast<int>(std::ceil(desiredPixelHeight / density)));
        glfwSetWindowSizeLimits(
            window,
            std::max(1, static_cast<int>(std::ceil(
                            (2 * opts.border + fontpk->getPx()) / density))),
            std::max(1, static_cast<int>(std::ceil(
                            (2 * opts.border + fontpk->getPy()) / density))),
            GLFW_DONT_CARE, GLFW_DONT_CARE);
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

        renderer = std::make_unique<Renderer>(window, fontpk.get());
        setupSignals();
        const int ptyFd = startShell(progPath, shellArgv);
        vt = std::make_unique<Vterm>(
            fontpk->getPx(), fontpk->getPy(), pixelWidth, pixelHeight, ptyFd);
        vt->setRefreshHandler(
            [](const Frame& frame) {
            renderer->update(frame);
        });
        vt->setOscHandler(
            [](int command, const std::string& argument) {
            handleOsc(command, argument);
        });
        vt->setBellHandler(
            []() {
            glfwRequestWindowAttention(window);
        });
        setupCallbacks();
        vt->setHasFocus(
            glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE);
        vt->resize(pixelWidth, pixelHeight);
        vt->redraw();

        {
            PtyEventSource ptySource(ptyFd);
            eventLoop(ptySource);
        }

        vt.reset();
        close(ptyFd);
        renderer.reset();
        fontpk.reset();
        if (cursor != nullptr) {
            glfwDestroyCursor(cursor);
            cursor = nullptr;
        }
        if (hyperlinkCursor != nullptr) {
            glfwDestroyCursor(hyperlinkCursor);
            hyperlinkCursor = nullptr;
        }
        glfwDestroyWindow(window);
        window = nullptr;
        glfwTerminate();
        glfwInitialized = false;
        return 0;
    }

    void emergencyCleanup() {
        vt.reset();
        renderer.reset();
        fontpk.reset();
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
}

int main(int argc, char* argv[]) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        emergencyCleanup();
        std::cerr << "Error: " << error.what() << std::endl;
        return 1;
    }
}
