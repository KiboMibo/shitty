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
#include "input_bindings.h"
#include "listener.h"
#include "options.h"
#include "pty.h"
#include "pty_output.h"
#include "render.h"
#include "startup.h"
#include "test_input.h"
#include "test_mode.h"
#include "vterm.h"
#include "vterm_host.h"

#include <plt/platform.h>
#include <plt/window.h>

#include <std/alg/minmax.h>
#include <std/ios/sys.h>
#include <std/str/view.h>
#include <std/sys/crt.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <langinfo.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include <std/mem/obj_pool.h>

using namespace stl;
using namespace plt;

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

    struct CallFontChanged final: public Listener {
        explicit CallFontChanged(ApplicationImpl* application);

        void onListen(void*) override;

        ApplicationImpl* application;
    };

    struct ApplicationImpl final: public Application, public VtermHost, public plt::WindowEvents, public plt::FrameCallback {
        explicit ApplicationImpl(Composer& composer);
        ~ApplicationImpl();

        int run(int argc, char* argv[]) override;
        void defer() override;
        void osc(int command, StringView argument) override;
        bool handlesOsc() const override;
        void title(StringView value) override;
        void cwd(StringView path) override;
        void bell() override;
        void leds(u8) override;
        void notify(StringView id, StringView title, StringView body, bool close) override;
        void progress(u32 state, u32) override;
        void windowOperation(u32 operation, u32 first, u32 second) override;
        VtermWindowInfo windowInfo() override;
        Clipboard* clipboard() override;
        DesktopActions* desktopActions() override;
        void close() override;
        bool frame(const plt::WindowInfo& info) override;

        Composer& composer;
        ObjPool* fontpackPool = nullptr;
        Clipboard* clipboard_ = nullptr;
        DesktopActions* desktopActions_ = nullptr;
        plt::WindowInfo windowInfo_;
        bool titleSet = false;
        u16 initialFontSize = 0;
        u16 logicalBorder = 0;

        int takeTestFd(int& argc, char* argv[]);
        static void childSignalHandler(int signal, siginfo_t* info, void*);
        void setupSignals();
        int startShell(const char* execPath, const char* const argv[]);
        bool presentTerminal();
        bool eventLoop();
        void updateWindowInfo(const plt::WindowInfo& info);
        void showWindow();
        void checkLocale();
        void fontInc();
        void fontDec();
        void fontReset();
        void fontChanged();
        void setFontSize(u16 size);
        void contentScaleChanged();
        void replaceFontpack(u16 size);
        void publishFontChanged();
        void wire();
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

CallFontChanged::CallFontChanged(ApplicationImpl* application_)
    : application(application_)
{
}

void CallFontChanged::onListen(void*) {
    application->fontChanged();
}

ApplicationImpl::ApplicationImpl(Composer& composer_)
    : composer(composer_)
{
}

void ApplicationImpl::wire() {
    composer.fontIncListeners.pushBack(composer.pool->make<CallFontInc>(this));
    composer.fontDecListeners.pushBack(composer.pool->make<CallFontDec>(this));
    composer.fontResetListeners.pushBack(composer.pool->make<CallFontReset>(this));
    composer.contentScaleChangedListeners.pushBack(composer.pool->make<CallContentScaleChanged>(this));
    composer.fontChangedListeners.pushBack(composer.pool->make<CallFontChanged>(this));
    composer.inputBindings->add({InputKey::Printable, InputControl | InputShift, '=', '+'}, &composer.fontIncListeners);
    composer.inputBindings->add({InputKey::Printable, InputControl, '-', '-'}, &composer.fontDecListeners);
    composer.inputBindings->add({InputKey::Printable, InputControl, '0', '0'}, &composer.fontResetListeners);
}

ApplicationImpl::~ApplicationImpl() {
    delete fontpackPool;
}

void ApplicationImpl::defer() {
    if (composer.window != nullptr) {
        composer.window->requestFrame();
    }
}

void ApplicationImpl::publishFontChanged() {
    for (IntrusiveNode* node = composer.fontChangedListeners.mutFront(); node != composer.fontChangedListeners.mutEnd();) {
        Listener* const listener = static_cast<Listener*>(node);
        node = node->next;
        listener->onListen();
    }
}

void ApplicationImpl::replaceFontpack(u16 size) {
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
        next = Fontpack::create(composer, *nextPool, opts.fontname, opts.dwfontname, pixels);
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
}

void ApplicationImpl::fontChanged() {
    const u16 columns = composer.columns == 0 ? opts.nCols : composer.columns;
    const u16 rows = composer.rows == 0 ? opts.nRows : composer.rows;
    const u32 border = 2u * opts.border;
    if (composer.window != nullptr) {
        composer.window->requestMinimumSize(border + composer.glyphWidth, border + composer.glyphHeight);
        composer.window->requestResizeUnit(composer.glyphWidth, composer.glyphHeight, border, border);
        composer.window->requestResize(border + (u32)(columns)*composer.glyphWidth, border + (u32)(rows)*composer.glyphHeight);
        return;
    }
    composer.resize((u16)(min(border + (u32)(columns)*composer.glyphWidth, (u32)(UINT16_MAX))), (u16)(min(border + (u32)(rows)*composer.glyphHeight, (u32)(UINT16_MAX))));
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
    const pid_t pid = pty_fork(ptyFd, composer.columns, composer.rows, composer.columns * composer.glyphWidth, composer.rows * composer.glyphHeight);
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
    if (vterm == nullptr || composer.renderer == nullptr) {
        return false;
    }
    const TerminalUpdate* const output = vterm->output();
    if (output == nullptr) {
        const bool repainted = composer.renderer->repaint();
        if (!repainted) {
            composer.window->requestFrame();
        }
        return repainted;
    }
    const bool presented = composer.renderer->update(*output);
    if (!presented) {
        composer.window->requestFrame();
        return false;
    }
    vterm->consume();
    return true;
}

void ApplicationImpl::close() {
    composer.platform->stop();
}

void ApplicationImpl::updateWindowInfo(const plt::WindowInfo& info) {
    windowInfo_ = info;
    if (isfinite(info.contentScale) && info.contentScale > 0.0f) {
        composer.setContentScale(info.contentScale);
    }
    composer.resize((u16)(min(info.width, (u32)(UINT16_MAX))), (u16)(min(info.height, (u32)(UINT16_MAX))));
}

bool ApplicationImpl::frame(const plt::WindowInfo& info) {
    updateWindowInfo(info);
    if (composer.vterm == nullptr) {
        return false;
    }
    return presentTerminal();
}

void ApplicationImpl::osc(int, StringView) {
}

bool ApplicationImpl::handlesOsc() const {
    return composer.vterm != nullptr;
}

void ApplicationImpl::title(StringView value) {
    titleSet = value != StringView(opts.title);
    composer.window->requestTitle(value);
}

void ApplicationImpl::cwd(StringView path) {
    if (!titleSet) {
        composer.window->requestTitle(path);
    }
}

void ApplicationImpl::bell() {
    composer.window->requestAttention();
}

void ApplicationImpl::leds(u8) {
}

void ApplicationImpl::notify(StringView, StringView, StringView, bool close) {
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
            composer.window->requestRestore();
            return;
        case 2:
            composer.window->requestIconify();
            return;
        case 3:
            composer.window->requestMove((i32)(first), (i32)(second));
            return;
        case 5:
            composer.window->requestFocus();
            return;
        case 7:
            composer.window->requestFrame();
            return;
        case 9: {
            if (first == 0) {
                composer.window->requestMaximized(false);
            } else if (first == 1) {
                composer.window->requestMaximized(true);
            } else if (first == 2) {
                composer.window->requestMaximized(!windowInfo_.maximized);
            }
            return;
        }
        case 10: {
            composer.window->requestFullscreen(first == 1 || (first == 2 && !windowInfo_.fullscreen));
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
    composer.window->requestResize(pixelWidth, pixelHeight);
}

VtermWindowInfo ApplicationImpl::windowInfo() {
    return {
        .x = windowInfo_.x,
        .y = windowInfo_.y,
        .screenPixelWidth = windowInfo_.screenPixelWidth,
        .screenPixelHeight = windowInfo_.screenPixelHeight,
        .iconified = windowInfo_.iconified,
        .maximized = windowInfo_.maximized,
        .fullscreen = windowInfo_.fullscreen,
    };
}

Clipboard* ApplicationImpl::clipboard() {
    return clipboard_;
}

DesktopActions* ApplicationImpl::desktopActions() {
    return desktopActions_;
}

bool ApplicationImpl::eventLoop() {
    composer.platform->run();
    return true;
}

void ApplicationImpl::showWindow() {
    const u32 border = 2u * opts.border;
    const u32 width = border + (u32)(opts.nCols) * composer.glyphWidth;
    const u32 height = border + (u32)(opts.nRows) * composer.glyphHeight;
    composer.window->requestShow();
    composer.resize((u16)(min(width, (u32)(UINT16_MAX))), (u16)(min(height, (u32)(UINT16_MAX))));
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
        return runTestMode(composer, *TestInput::create(composer), testFd, argc, argv);
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

    composer.platform = plt::Platform::create(*composer.pool);
    composer.window = composer.platform->createWindow(
        *composer.pool,
        {
            .appId = StringView(u8"shitty"),
            .title = StringView(opts.title),
            .width = (u32)(max(320, (int)(opts.nCols) * opts.fontsize / 2)),
            .height = (u32)(max(200, (int)(opts.nRows) * opts.fontsize)),
            .input = composer.input,
            .events = this,
            .frame = this,
        }
    );
    clipboard_ = Clipboard::create(composer, *composer.window);
    desktopActions_ = DesktopActions::create(composer, *composer.window);
    contentScaleChanged();

    replaceFontpack(initialFontSize);
    showWindow();

    setupSignals();
    const int ptyFd = startShell(launch.executable.c_str(), shellArgv.data());
    composer.pty = Pty::adopt(composer, ptyFd);
    composer.ptyOutputs = PtyOutputQueue::create(composer.pool, composer.smallObjects, *composer.pty);
    composer.ptyOutput = composer.ptyOutputs->append();

    composer.renderer = Renderer::create(composer, composer.window->renderContext());
    composer.vterm = Vterm::create(composer, *this, nullptr);
    defer();

    eventLoop();
    return 0;
}

Application* Application::create(Composer& composer) {
    ApplicationImpl* const application = composer.pool->make<ApplicationImpl>(composer);
    application->wire();
    return application;
}
