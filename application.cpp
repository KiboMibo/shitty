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
#include "fd_redirect.h"
#include "font_pack.h"
#include "input_bindings.h"
#include "listener.h"
#include "options.h"
#include "pty.h"
#include "pty_event_source.h"
#include "vk_renderer.h"
#include "startup.h"
#include "test_mode.h"
#include "vterm.h"
#include "vterm_host.h"
#include "window.h"

#include <std/ios/sys.h>
#include <std/str/view.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <langinfo.h>
#include <limits.h>
#include <optional>
#include <poll.h>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    struct ApplicationImpl;

    struct CallFontInc final: public Listener {
        explicit CallFontInc(ApplicationImpl* application);

        void onListen(void*) override;

        ApplicationImpl* application;
    };

    struct CallFontDec final: public Listener {
        explicit CallFontDec(ApplicationImpl* application);

        void onListen(void*) override;

        ApplicationImpl* application;
    };

    struct CallFontReset final: public Listener {
        explicit CallFontReset(ApplicationImpl* application);

        void onListen(void*) override;

        ApplicationImpl* application;
    };

    struct CallContentScaleChanged final: public Listener {
        explicit CallContentScaleChanged(ApplicationImpl* application);

        void onListen(void*) override;

        ApplicationImpl* application;
    };

    struct ApplicationImpl final: public Application, public VtermHost {
        explicit ApplicationImpl(Composer& composer);
        ~ApplicationImpl();

        int run(int argc, char* argv[]) override;
        void osc(int command, const std::string& argument) override;
        bool handlesOsc() const override;
        void title(StringView value) override;
        void cwd(StringView path) override;
        void bell() override;
        bool handlesPrinter() const override;
        void print(const std::string& output) override;
        void leds(u8) override;
        void notify(const std::string&, const std::string& title, const std::string& body, bool close) override;
        void progress(u32 state, u32) override;
        void windowOperation(u32 operation, u32 first, u32 second) override;
        VtermWindowInfo windowInfo() override;

        Composer& composer;
        ObjPool* fontpackPool = nullptr;
        FILE* printerPipe = nullptr;
        bool refreshAllowed = true;
        bool refreshPending = false;
        bool committedRepaintPending = false;
        bool frameReady = true;
        bool titleSet = false;
        u16 initialFontSize = 0;
        u16 logicalBorder = 0;
        std::optional<std::chrono::steady_clock::time_point> refreshDeadline;

        int takeTestFd(int& argc, char* argv[]);
        static void childSignalHandler(int signal, siginfo_t* info, void*);
        void setupSignals();
        int startShell(const char* execPath, const char* const argv[]);
        bool presentTerminal();
        bool repaint();
        bool flushPtyOutput();
        bool readPty();
        bool servicePty(bool readable, bool writable);
        bool eventLoop();
        void checkLocale();
        void fontInc();
        void fontDec();
        void fontReset();
        void setFontSize(u16 size);
        void contentScaleChanged();
        void replaceFontpack(u16 size);
        void publishFontChanged();
    };
}

CallFontInc::CallFontInc(ApplicationImpl* application_)
    : application(application_)
{
}

void CallFontInc::onListen(void*) {
    application->fontInc();
}

CallFontDec::CallFontDec(ApplicationImpl* application_)
    : application(application_)
{
}

void CallFontDec::onListen(void*) {
    application->fontDec();
}

CallFontReset::CallFontReset(ApplicationImpl* application_)
    : application(application_)
{
}

void CallFontReset::onListen(void*) {
    application->fontReset();
}

CallContentScaleChanged::CallContentScaleChanged(ApplicationImpl* application_)
    : application(application_)
{
}

void CallContentScaleChanged::onListen(void*) {
    application->contentScaleChanged();
}

ApplicationImpl::ApplicationImpl(Composer& composer_)
    : composer(composer_)
{
    composer.fontIncListeners.pushBack(composer.pool->make<CallFontInc>(this));
    composer.fontDecListeners.pushBack(composer.pool->make<CallFontDec>(this));
    composer.fontResetListeners.pushBack(composer.pool->make<CallFontReset>(this));
    composer.contentScaleChangedListeners.pushBack(composer.pool->make<CallContentScaleChanged>(this));

    composer.inputBindings->add({InputKey::Printable, InputControl | InputShift, '=', '+'}, &composer.fontIncListeners);
    composer.inputBindings->add({InputKey::Printable, InputControl, '-', '-'}, &composer.fontDecListeners);
    composer.inputBindings->add({InputKey::Printable, InputControl, '0', '0'}, &composer.fontResetListeners);
}

ApplicationImpl::~ApplicationImpl() {
    composer.fonts = nullptr;
    delete fontpackPool;
    composer.application = nullptr;
}

void ApplicationImpl::publishFontChanged() {
    for (IntrusiveNode* node = composer.fontChangedListeners.mutFront(); node != composer.fontChangedListeners.mutEnd();) {
        Listener* const listener = static_cast<Listener*>(node);
        node = node->next;
        listener->onListen();
    }
}

void ApplicationImpl::replaceFontpack(u16 size) {
    const u16 columns = composer.columns == 0 ? opts.nCols : composer.columns;
    const u16 rows = composer.rows == 0 ? opts.nRows : composer.rows;
    ObjPool* const previousPool = fontpackPool;
    Fontpack* const previousFonts = composer.fonts;
    const u16 previousFontSize = composer.fontSize;
    const u16 previousGlyphWidth = composer.glyphWidth;
    const u16 previousGlyphHeight = composer.glyphHeight;
    ObjPool* const nextPool = ObjPool::fromMemoryRaw();
    Fontpack* next;
    try {
        int scaled = (int)(size * composer.contentScale + 0.5f);
        scaled = scaled < 1 ? 1 : scaled > 255 ? 255 : scaled;
        const u16 pixels = (u16)(scaled);
        next = Fontpack::create(*nextPool, opts.fontname, opts.dwfontname, pixels);
    } catch (...) {
        delete nextPool;
        throw;
    }

    fontpackPool = nextPool;
    composer.fontSize = size;
    composer.fonts = next;
    composer.setGlyphSize(next->getPx(), next->getPy());
    try {
        publishFontChanged();
    } catch (...) {
        fontpackPool = previousPool;
        composer.fontSize = previousFontSize;
        composer.fonts = previousFonts;
        composer.glyphWidth = previousGlyphWidth;
        composer.glyphHeight = previousGlyphHeight;
        delete nextPool;
        throw;
    }
    delete previousPool;
    if (columns != 0 && rows != 0) {
        composer.window->resizePixels(2u * opts.border + (u32)(columns)*composer.glyphWidth, 2u * opts.border + (u32)(rows)*composer.glyphHeight);
    }
}

void ApplicationImpl::setFontSize(u16 size) {
    if (composer.fontSize == size) {
        return;
    }
    try {
        replaceFontpack(size);
    } catch (...) {
    }
}

void ApplicationImpl::fontInc() {
    if (composer.fontSize < 255) {
        setFontSize(composer.fontSize + 1);
    }
}

void ApplicationImpl::fontDec() {
    if (composer.fontSize > 1) {
        setFontSize(composer.fontSize - 1);
    }
}

void ApplicationImpl::fontReset() {
    setFontSize(initialFontSize);
}

void ApplicationImpl::contentScaleChanged() {
    const u16 previousBorder = opts.border;
    int scaledBorder = (int)(logicalBorder * composer.contentScale + 0.5f);
    scaledBorder = scaledBorder < 0 ? 0 : scaledBorder > 3000 ? 3000 : scaledBorder;
    opts.border = (u16)(scaledBorder);
    if (fontpackPool != nullptr) {
        try {
            replaceFontpack(composer.fontSize);
        } catch (...) {
            opts.border = previousBorder;
        }
    }
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

void ApplicationImpl::childSignalHandler(int signal, siginfo_t* info, void*) {
    if (signal == SIGCHLD) {
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
    const pid_t pid = pty_fork(ptyFd, composer.columns, composer.rows);
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

bool ApplicationImpl::presentTerminal() {
    Vterm* const vterm = composer.vterm;
    Renderer* const renderer = composer.renderer;
    Window* const window = composer.window;
    while (true) {
        const VtermOutput output = vterm->output();
        if (output.terminal == nullptr) {
            refreshPending = false;
            return true;
        }
        refreshPending = true;
        if (!refreshAllowed || !frameReady) {
            return false;
        }
        const bool paced = window->requestFrame();
        if (!renderer->update(*output.terminal)) {
            if (paced) {
                window->cancelFrame();
            }
            return false;
        }
        frameReady = !paced;
        vterm->consume(VtermConsume{0, true});
    }
}

bool ApplicationImpl::repaint() {
    if (!frameReady) {
        return false;
    }
    Window* const window = composer.window;
    const bool paced = window->requestFrame();
    if (!composer.renderer->repaint()) {
        if (paced) {
            window->cancelFrame();
        }
        return false;
    }
    frameReady = !paced;
    return true;
}

bool ApplicationImpl::flushPtyOutput() {
    Vterm* const vterm = composer.vterm;
    Pty* const pty = composer.pty;
    while (true) {
        const VtermOutput output = vterm->output();
        if (output.pty.empty()) {
            return true;
        }
        const ssize_t count = pty->write(output.pty.data(), output.pty.length());
        if (count > 0) {
            vterm->consume({(size_t)(count), false});
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
    Vterm* const vterm = composer.vterm;
    Pty* const pty = composer.pty;
    u8 buffer[8192];
    size_t drained = 0;
    bool finished = false;
    while (drained < maxDrainBytes) {
        const ssize_t count = pty->read(buffer, sizeof(buffer));
        if (count > 0) {
            vterm->feedPty(StringView(buffer, count));
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

void ApplicationImpl::osc(int, const std::string&) {
}

bool ApplicationImpl::handlesOsc() const {
    return composer.vterm != nullptr;
}

void ApplicationImpl::title(StringView value) {
    titleSet = value != StringView(opts.title);
    composer.window->setTitle(value);
}

void ApplicationImpl::cwd(StringView path) {
    if (!titleSet) {
        composer.window->setTitle(path);
    }
}

void ApplicationImpl::bell() {
    composer.window->requestAttention();
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
    composer.window->requestAttention();
}

void ApplicationImpl::progress(u32 state, u32) {
    if (state == 2 || state == 4) {
        composer.window->requestAttention();
    }
}

void ApplicationImpl::windowOperation(u32 operation, u32 first, u32 second) {
    switch (operation) {
        case 1:
            composer.window->restore();
            return;
        case 2:
            composer.window->iconify();
            return;
        case 3:
            composer.window->move((i32)(first), (i32)(second));
            return;
        case 5:
            composer.window->focus();
            return;
        case 7:
            composer.window->requestRedraw();
            return;
        case 9: {
            const WindowInfo current = composer.window->info();
            if (first == 0) {
                composer.window->setMaximized(false);
            } else if (first == 1) {
                composer.window->setMaximized(true);
            } else if (first == 2) {
                composer.window->setMaximized(!current.maximized);
            }
            return;
        }
        case 10: {
            const WindowInfo current = composer.window->info();
            composer.window->setFullscreen(first == 1 || (first == 2 && !current.fullscreen));
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
    composer.window->resizePixels(pixelWidth, pixelHeight);
}

VtermWindowInfo ApplicationImpl::windowInfo() {
    const WindowInfo source = composer.window->info();
    return {
        .x = source.x,
        .y = source.y,
        .screenPixelWidth = source.screenPixelWidth,
        .screenPixelHeight = source.screenPixelHeight,
        .iconified = source.iconified,
        .maximized = source.maximized,
        .fullscreen = source.fullscreen,
    };
}

bool ApplicationImpl::eventLoop() {
    using Clock = std::chrono::steady_clock;
    constexpr auto resizeGrace = std::chrono::milliseconds(10);
    constexpr auto retryDelay = std::chrono::milliseconds(10);
    Window* const eventWindow = composer.window;
    Vterm* const vterm = composer.vterm;
    PtyEventSource* const ptySource = composer.ptyEvents;
    while (true) {
        ptySource->setWriteInterest(!vterm->output().pty.empty());
        refreshAllowed = false;
        const VtermState initialState = vterm->state();
        double timeout = (initialState.synchronizedOutput || initialState.animation) ? 0.05 : -1.0;
        if (frameReady && (refreshPending || committedRepaintPending)) {
            const auto now = Clock::now();
            const double refreshTimeout = refreshDeadline ? std::max(0.0, std::chrono::duration<double>(*refreshDeadline - now).count()) : 0.0;
            timeout = timeout < 0.0 ? refreshTimeout : std::min(timeout, refreshTimeout);
        }
        const WindowEvents events = eventWindow->dispatchEvents(timeout);
        if (events.close) {
            return true;
        }
        if (events.frameReady) {
            frameReady = true;
        }
        flushPtyOutput();
        vterm->expireSynchronizedOutput(false);
        if (vterm->advanceAnimation(false)) {
            vterm->expose();
        }
        bool resized = false;
        if (events.resized) {
            const VtermState resizedState = vterm->state();
            committedRepaintPending = resizedState.synchronizedOutput;
            if (!committedRepaintPending) {
                vterm->expose();
            }
            resized = true;
            refreshDeadline = Clock::now() + resizeGrace;
        } else if (events.redraw) {
            if (vterm->state().synchronizedOutput) {
                committedRepaintPending = true;
            } else {
                vterm->expose();
            }
        }
        const short ptyEvents = ptySource->events();
        const bool readPtyInput = ptyEvents & (POLLIN | POLLHUP | POLLERR);
        const bool finished = servicePty(readPtyInput, ptyEvents & POLLOUT);
        if (readPtyInput) {
            ptySource->acknowledge();
            if (finished) {
                return false;
            }
        } else if (ptyEvents && !(ptyEvents & (POLLIN | POLLHUP | POLLERR))) {
            ptySource->acknowledge();
        }
        flushPtyOutput();
        ptySource->setWriteInterest(!vterm->output().pty.empty());
        presentTerminal();

        // A child responding to SIGWINCH is part of the same visual
        // update. Give it a short opportunity to redraw, then fall back
        // to the resized terminal state even if it produces no output.
        if (readPtyInput) {
            refreshDeadline.reset();
        }
        const auto now = Clock::now();
        if (!vterm->state().synchronizedOutput) {
            committedRepaintPending = false;
        }
        if (frameReady && committedRepaintPending && (!refreshDeadline || now >= *refreshDeadline)) {
            if (repaint()) {
                committedRepaintPending = false;
                refreshDeadline.reset();
            } else {
                refreshDeadline = now + retryDelay;
            }
        }
        if (frameReady && refreshPending && (!refreshDeadline || now >= *refreshDeadline)) {
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

int ApplicationImpl::run(int argc, char* argv[]) {
    int testFd = -1;
#ifdef SHITTY_FOR_TESTS
    testFd = takeTestFd(argc, argv);
#endif
    Window* const window = testFd < 0 ? Window::create(composer) : Window::createHeadless(composer);
    checkLocale();
    opts.initialize(&argc, argv);
    opts.parse();
    initialFontSize = opts.fontsize;
    logicalBorder = opts.border;
    composer.fontSize = initialFontSize;
    if (opts.verbose) {
        opts.printVersion();
    }
    if (setenv("SHITTY_VERSION", SHITTY_VERSION, 1) < 0) {
        sysError("setenv SHITTY_VERSION");
    }
    if (testFd >= 0) {
        return runTestMode(composer, *window->testApi(), testFd, argc, argv);
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

    composer.window->initialize();
    contentScaleChanged();

    replaceFontpack(initialFontSize);
    composer.window->show();

    setupSignals();
    const int ptyFd = startShell(launch.executable.c_str(), shellArgv.data());
    composer.pty = Pty::adopt(composer, ptyFd);

    composer.renderer = composer.window->createRender();
    if (opts.printerCommand[0] != '\0') {
        printerPipe = popen(opts.printerCommand, "w");
        if (printerPipe == nullptr) {
            throw std::runtime_error("Cannot start printer command");
        }
    }
    composer.vterm = Vterm::create(composer, *this, nullptr);
    composer.window->activate();
    presentTerminal();

    composer.ptyEvents = PtyEventSource::create(composer);
    eventLoop();

    if (printerPipe != nullptr) {
        pclose(printerPipe);
        printerPipe = nullptr;
    }
    return 0;
}

Application* Application::create(Composer& composer) {
    return composer.pool->make<ApplicationImpl>(composer);
}
