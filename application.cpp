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
#include "brand.h"
#include "composer.h"
#include "drop_target.h"
#include "fatal.h"
#include "font_pack.h"
#include "input_bindings.h"
#include "input_remap.h"
#include "listener.h"
#include "options.h"
#include "session.h"
#include "tab_bar_input.h"
#include "pty.h"
#include "render.h"
#include "startup.h"

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
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/crt.h>
#include <std/sys/throw.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <langinfo.h>
#include <locale.h>
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

    struct CallNewTab final: public Listener {
        explicit CallNewTab(ApplicationImpl* application);

        void onListen(void*) override;

        ApplicationImpl* application;
    };

    struct CallCloseTab final: public Listener {
        explicit CallCloseTab(ApplicationImpl* application);

        void onListen(void*) override;

        ApplicationImpl* application;
    };

    struct CallPrevTab final: public Listener {
        explicit CallPrevTab(ApplicationImpl* application);

        void onListen(void*) override;

        ApplicationImpl* application;
    };

    struct CallNextTab final: public Listener {
        explicit CallNextTab(ApplicationImpl* application);

        void onListen(void*) override;

        ApplicationImpl* application;
    };

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
        // The terminals behind this window. Null until run() builds the
        // first one, so the tab actions guard on it.
        SessionSet* sessions_ = nullptr;
        // Kept so a new tab can start the same shell the window started
        // with. LaunchCommand owns a Buffer and is move-only, and argv
        // outlives the process anyway, so the command is rebuilt per tab
        // rather than stored.
        int argc_ = 0;
        char** argv_ = nullptr;
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
        void newTab();
        void closeTab();
        void prevTab();
        void nextTab();
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
    composer.newTabListeners.pushBack(composer.pool->make<CallNewTab>(this));
    composer.prevTabListeners.pushBack(composer.pool->make<CallPrevTab>(this));
    composer.nextTabListeners.pushBack(composer.pool->make<CallNextTab>(this));
    composer.closeTabListeners.pushBack(composer.pool->make<CallCloseTab>(this));
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
        next = Fontpack::create(composer, *nextPool, composer.opts->fontnames.data(), composer.opts->fontnames.length(), pixels);
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
    const u16 columns = sized && composer.columns != 0 ? composer.columns : composer.opts->nCols;
    const u16 rows = sized && composer.rows != 0 ? composer.rows : composer.opts->nRows;
    const u32 border = 2u * composer.opts->border;
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

CallNewTab::CallNewTab(ApplicationImpl* application_)
    : application(application_)
{
}

void CallNewTab::onListen(void*) {
    application->newTab();
}

CallCloseTab::CallCloseTab(ApplicationImpl* application_)
    : application(application_)
{
}

void CallCloseTab::onListen(void*) {
    application->closeTab();
}

void ApplicationImpl::closeTab() {
    if (sessions_ == nullptr) {
        return;
    }
    if (sessions_->closeActive()) {
        composer.window->requestFrame();
        return;
    }
    composer.window->requestClose();
}

CallPrevTab::CallPrevTab(ApplicationImpl* application_)
    : application(application_)
{
}

void CallPrevTab::onListen(void*) {
    application->prevTab();
}

CallNextTab::CallNextTab(ApplicationImpl* application_)
    : application(application_)
{
}

void CallNextTab::onListen(void*) {
    application->nextTab();
}

void ApplicationImpl::prevTab() {
    if (sessions_ != nullptr && sessions_->activatePrevious()) {
        composer.window->requestFrame();
    }
}

void ApplicationImpl::nextTab() {
    if (sessions_ != nullptr && sessions_->activateNext()) {
        composer.window->requestFrame();
    }
}

void ApplicationImpl::newTab() {
    if (sessions_ == nullptr) {
        return;
    }
    // The new terminal reads composer.pty at construction to claim its
    // own, so the pty is published before the terminal is built and the
    // pair is handed to the session set together.
    const LaunchCommand launch = buildLaunchCommand(argc_, argv_, composer.opts->shell, composer.opts->login);
    Pty* const pty = Pty::create(composer, launch);
    composer.pty = pty;
    composer.ptyOutput = pty->output();
    Vterm* const terminal = Vterm::create(composer, nullptr);
    sessions_->activate(sessions_->adopt(terminal, pty));
    composer.window->requestFrame();
    if (composer.opts->verbose) {
        fprintf(stderr, "%s: session: opened, %zu total\n", composer.brand->identifierCString(), sessions_->count());
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
    const u16 previousBorder = composer.opts->border;
    int scaledBorder = (int)(logicalBorder * composer.contentScale + 0.5f);
    scaledBorder = scaledBorder < 0 ? 0 : scaledBorder > 3000 ? 3000 : scaledBorder;
    composer.opts->border = (u16)(scaledBorder);
    if (fontpackPool != nullptr) {
        try {
            replaceFontpack(composer.fontSize);
        } catch (...) {
            composer.opts->border = previousBorder;
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
        if (strcmp(argv[k], "--test-fd") != 0) {
            continue;
        }
        if (k + 1 >= argc) {
            raiseError(StringView(u8"--test-fd requires a descriptor"));
        }
        char* end = nullptr;
        const long fd = strtol(argv[k + 1], &end, 10);
        if (end == argv[k + 1] || *end || fd < 0 || fd > INT_MAX) {
            raiseError(StringView(u8"invalid --test-fd descriptor"));
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

// The status of the most recently reaped child. The signal handler
// records it; ApplicationImpl::close exits with it once the last session
// is gone, which is the only place that knows the process is ending.
static volatile sig_atomic_t lastChildStatus = 0;

void ApplicationImpl::childSignalHandler(int signal, siginfo_t*, void*) {
    // SIGCHLD does not queue: one delivery may stand for several exited
    // children (the shell plus xdg-open helpers), so reap until drained.
    if (signal == SIGCHLD) {
        int status = 0;
        pid_t pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            // Reap only. Which shell dying ends the process is not
            // decidable here: the answer depends on how many sessions are
            // left, and that races with the close this same death is
            // about to trigger through the pty's EOF path. That path owns
            // the decision; the status is recorded for it to exit with.
            lastChildStatus = (sig_atomic_t)(WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
        }
    }
}

void ApplicationImpl::setupSignals() {
    struct sigaction childAction{};
    childAction.sa_sigaction = childSignalHandler;
    childAction.sa_flags = SA_SIGINFO | SA_RESTART | SA_NOCLDSTOP;
    sigemptyset(&childAction.sa_mask);
    if (sigaction(SIGCHLD, &childAction, nullptr) < 0) {
        Errno().raise(StringBuilder() << StringView(u8"can't install SIGCHLD handler: sigaction()"));
    }

    struct sigaction defaultAction{};
    defaultAction.sa_handler = SIG_DFL;
    sigemptyset(&defaultAction.sa_mask);
    if (sigaction(SIGINT, &defaultAction, nullptr) < 0) {
        Errno().raise(StringBuilder() << StringView(u8"can't reset SIGINT handler: sigaction()"));
    }
    if (sigaction(SIGQUIT, &defaultAction, nullptr) < 0) {
        Errno().raise(StringBuilder() << StringView(u8"can't reset SIGQUIT handler: sigaction()"));
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
    composer.window->requestTextInputRect((i32)(composer.opts->border + (u32)(output->cursor.posX) * composer.glyphWidth), (i32)(composer.opts->border + (u32)(output->cursor.posY) * composer.glyphHeight), composer.glyphWidth, composer.glyphHeight);
    vterm->consume();
    return true;
}

void ApplicationImpl::close() {
#if defined(SHITTY_FOR_TESTS)
    composer.platform->stop();
#else
    // Exit with the shell's status only when a dying shell is what ended
    // the window: liveSessions reaches zero only through close(). Closing
    // the window yourself, with sessions still live, is not a shell's
    // failure and must not borrow the status of one that ended earlier.
    _exit(SessionSet::liveSessions == 0 ? (int)(lastChildStatus) : 0);
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
    const u32 border = 2u * composer.opts->border;
    const u32 width = border + (u32)(composer.opts->nCols) * composer.glyphWidth;
    const u32 height = border + (u32)(composer.opts->nRows) * composer.glyphHeight;
    composer.window->requestShow();
    composer.resize((u16)(min(width, (u32)(UINT16_MAX))), (u16)(min(height, (u32)(UINT16_MAX))));
}

void ApplicationImpl::checkLocale() {
    const char* locale = setlocale(LC_ALL, "");
    if (locale == nullptr) {
        sysO << StringView(u8"Warning: could not set locale; international input may be broken.") << endL;
        return;
    }
    if (strcmp(nl_langinfo(CODESET), "UTF-8") != 0) {
        sysO << StringView(u8"Warning: non-UTF-8 locale ") << StringView(locale) << StringView(u8"; international input may be broken.") << endL;
    }
}

int ApplicationImpl::run(int argc, char* argv[]) {
    int testFd = -1;
#ifdef SHITTY_FOR_TESTS
    testFd = takeTestFd(argc, argv);
#endif
    checkLocale();
    // In the parent, before any thread exists. TERM and the version are
    // process-wide constants identical for every terminal behind the
    // window, and setenv() must never run in a forked child of a
    // multithreaded process: glibc's environ lock is not reset at fork.
    configureTerminalChildEnvironment(*composer.brand);
    composer.opts = Options::create(*composer.pool, *composer.brand, argv, argc);
    argc = 0;
    while (argv[argc] != nullptr) {
        ++argc;
    }
    initialFontSize = composer.opts->fontsize;
    logicalBorder = composer.opts->border;
    composer.fontSize = initialFontSize;
    composer.inputRemap = InputRemap::create(composer);
    if (testFd >= 0) {
        return runTestMode(composer, *TestInput::create(composer), *this, *this, testFd, argc, argv);
    }

    argc_ = argc;
    argv_ = argv;
    const LaunchCommand launch = buildLaunchCommand(argc, argv, composer.opts->shell, composer.opts->login);
    if (argc > 2 && strcmp(argv[1], "-e") == 0) {
        if (composer.opts->titleSource != OptionSource::CmdLine && composer.opts->titleSource != OptionSource::Config) {
            composer.opts->title = argv[2];
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
            .appId = composer.brand->identifier(),
            .title = composer.opts->title,
            .width = (u32)(max(320, (int)(composer.opts->nCols) * composer.opts->fontsize / 2)),
            .height = (u32)(max(200, (int)(composer.opts->nRows) * composer.opts->fontsize)),
            .decorations = !composer.opts->noDecorations,
            .input = composer.input,
            .events = this,
            .frame = this,
            .drop = createDropTarget(*composer.pool, composer),
            .icon = composer.brand->iconData(),
            .appName = composer.brand->displayName(),
        }
    );
    contentScaleChanged();

    replaceFontpack(initialFontSize);
    showWindow();

    setupSignals();
    composer.pty = Pty::create(composer, launch);
    composer.ptyOutput = composer.pty->output();

    createRenderer();
    Vterm* const first = Vterm::create(composer, nullptr);
    sessions_ = SessionSet::create(composer);
    composer.sessions = sessions_;
    // Ahead of the terminal, which activate() keeps pushing to the back.
    composer.inputHandlers.pushBack(TabBarInput::create(composer));
    sessions_->adopt(first, composer.pty);
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
