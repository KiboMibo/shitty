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
#include "composer.h"
#include "drop_target.h"
#include "fd_redirect.h"
#include "font_pack.h"
#include "input_bindings.h"
#include "listener.h"
#include "options.h"
#include "pty.h"
#include "render.h"
#include "startup.h"

#include "icon_data.h"
#include "test_input.h"
#include "test_mode.h"
#include "vterm.h"

#include <plt/drop.h>
#include <plt/fiber.h>
#include <plt/input.h>
#include <plt/mutex.h>
#include <plt/platform.h>
#include <plt/window.h>

#include <std/alg/minmax.h>
#include <std/ios/sys.h>
#include <std/lib/vector.h>
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

    struct ApplicationImpl final: public Application, public plt::WindowEvents, public plt::FrameCallback {
        explicit ApplicationImpl(Composer& composer);
        ~ApplicationImpl();

        int run(int argc, char* argv[]) override;
        void close() override;
        bool frame(const plt::WindowInfo& info) override;

        Composer& composer;
        ObjPool* fontpackPool = nullptr;
        u16 initialFontSize = 0;
        u16 logicalBorder = 0;
        // True until the first frame supplies real metrics; -geometry is
        // applied against them exactly once.
        bool initialGeometryPending = true;

        int takeTestFd(int& argc, char* argv[]);
        void createRenderer();
        static void childSignalHandler(int signal, siginfo_t* info, void*);
        void setupSignals();
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
    composer.inputBindings->add(InputActions::IncFontSize, &composer.fontIncListeners);
    composer.inputBindings->add(InputActions::DecFontSize, &composer.fontDecListeners);
    composer.inputBindings->add(InputActions::ResetFontSize, &composer.fontResetListeners);
}

ApplicationImpl::~ApplicationImpl() {
    delete fontpackPool;
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
        Vector<StringView> names;
        for (size_t index = 0; index < opts.fontnameCount; ++index) {
            names.pushBack(StringView(opts.fontnames[index]));
        }
        next = Fontpack::create(composer, *nextPool, names.data(), names.length(), pixels);
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
    // Until the first frame reports real window metrics the requested
    // geometry wins: the pre-show window is a guess, and the grid derived
    // from it must not displace -geometry. Afterwards font changes keep
    // the grid the user has.
    const bool sized = !initialGeometryPending;
    const u16 columns = sized && composer.columns != 0 ? composer.columns : opts.nCols;
    const u16 rows = sized && composer.rows != 0 ? composer.rows : opts.nRows;
    const u32 border = 2u * opts.border;
    composer.window->requestMinimumSize(border + composer.glyphWidth, border + composer.glyphHeight);
    composer.window->requestResizeUnit(composer.glyphWidth, composer.glyphHeight, border, border);
    const plt::WindowInfo info = composer.window->info();
    if (info.fullscreen || info.maximized || info.tiled) {
        // The window is the screen's, the compositor's tile, or the
        // maximized frame - not ours to resize (issue 38, issue 46: a
        // self-resize under a tiler bounces against the compositor's
        // configure and every font step reflows twice). Let the next
        // frame reflow the grid over the same pixels.
        composer.window->requestFrame();
        return;
    }
    composer.window->requestResize(border + (u32)(columns)*composer.glyphWidth, border + (u32)(rows)*composer.glyphHeight);
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

void ApplicationImpl::createRenderer() {
    // Assigning the fresh pool destroys the previous one — and with it
    // the dead renderer and its listeners.
    composer.rendererPool = ObjPool::fromMemory();
    composer.renderer = Renderer::create(composer, *composer.rendererPool, composer.window->renderContext());
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

void ApplicationImpl::childSignalHandler(int signal, siginfo_t*, void*) {
    // SIGCHLD does not queue: one delivery may stand for several exited
    // children (the shell plus xdg-open helpers), so reap until drained.
    if (signal == SIGCHLD) {
        int status = 0;
        pid_t pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
#if !defined(SHITTY_FOR_TESTS)
            // The shell is the terminal's lifetime: exit right here with
            // its status, no teardown by design. The test binary instead
            // winds down through the destructors for the sanitizers.
            if (pid == ptyChildPid()) {
                _exit(WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
            }
#endif
        }
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

bool ApplicationImpl::presentTerminal() {
    Vterm* const vterm = composer.vterm;
    if (composer.renderer == nullptr) {
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
    // Keep the input-method candidate window anchored to the cursor cell.
    composer.window->requestTextInputRect((i32)(opts.border + (u32)(output->cursor.posX) * composer.glyphWidth), (i32)(opts.border + (u32)(output->cursor.posY) * composer.glyphHeight), composer.glyphWidth, composer.glyphHeight);
    vterm->consume();
    return true;
}

void ApplicationImpl::close() {
#if defined(SHITTY_FOR_TESTS)
    composer.platform->stop();
#else
    // The window is the other lifetime bound; same policy as SIGCHLD.
    _exit(0);
#endif
}

void ApplicationImpl::updateWindowInfo(const plt::WindowInfo& info) {
    if (isfinite(info.contentScale) && info.contentScale > 0.0f) {
        composer.setContentScale(info.contentScale);
    }
    composer.resize((u16)(min(info.width, (u32)(UINT16_MAX))), (u16)(min(info.height, (u32)(UINT16_MAX))));
    if (initialGeometryPending) {
        // The first real metrics (glyphs at the live content scale) size
        // the window to the requested geometry exactly once.
        fontChanged();
        initialGeometryPending = false;
    }
}

bool ApplicationImpl::frame(const plt::WindowInfo& info) {
    updateWindowInfo(info);
    if (composer.renderer == nullptr) {
        // The previous renderer died with its surface and dropped its own
        // pool; build a fresh one and repaint everything.
        createRenderer();
        composer.vterm->expose();
    }
    return presentTerminal();
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
        return runTestMode(composer, *TestInput::create(composer), *this, *this, testFd, argc, argv);
    }

    LaunchCommand launch = buildLaunchCommand(argc, argv, opts.shell, opts.login);
    if (argc > 2 && std::strcmp(argv[1], "-e") == 0) {
        if (opts.titleSource != OptionSource::CmdLine) {
            opts.title = argv[2];
        }
    }
    composer.platform = plt::Platform::create(*composer.pool);
    // Input deliveries run on one fiber, so handlers may block on the PTY
    // mutex or descriptor without stopping the event loop; later input
    // waits in the sink's queue.
    composer.input = plt::createFiberInputSink(*composer.pool, *composer.platform->scheduler(), *composer.input);
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
            .drop = createDropTarget(*composer.pool, composer),
            .icon = StringView((const u8*)(embeddedIcon.data), embeddedIcon.size),
        }
    );
    contentScaleChanged();

    replaceFontpack(initialFontSize);
    showWindow();

    setupSignals();
    composer.ptyMutex = composer.pool->make<plt::FiberMutex>();
    composer.pty = Pty::create(composer, launch);
    composer.ptyOutput = composer.pty->output();

    createRenderer();
    Vterm::create(composer, nullptr);
    composer.window->requestFrame();

    eventLoop();
    // The swapchain holds proxies of the platform display. The composer and
    // its rendererPool outlive the platform in the main pool, so destroying
    // the renderer there would touch Wayland objects after the display is
    // disconnected; drop it while the connection is still alive.
    composer.renderer = nullptr;
    composer.rendererPool = ObjPool::fromMemory();
    return 0;
}

Application* Application::create(Composer& composer) {
    ApplicationImpl* const application = composer.pool->make<ApplicationImpl>(composer);
    application->wire();
    return application;
}
