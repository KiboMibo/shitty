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
#include "configuration.h"
#include "drop_target.h"
#include "fatal.h"
#include "font_pack.h"
#include "grid_geometry.h"
#include "num.h"
#include "input_bindings.h"
#include "input_remap.h"
#include "listener.h"
#include "options.h"
#include "session.h"
#include "pty.h"
#include "quick_companion.h"
#include "quick_frame_store.h"
#include "render.h"
#include "startup.h"

#include "test_input.h"
#include "test_mode.h"
#include "ui_csd_tabs.h"
#include "ui_quick_hotkey.h"
#include "ui_sidebar_tabs.h"
#include "vterm.h"

#include <plt/drop.h>
#include <plt/fiber.h>
#include <plt/input.h>
#include <plt/mutex.h>
#include <plt/platform.h>
#include <plt/poller.h>
#include <plt/window.h>

#include <std/alg/defer.h>
#include <std/alg/minmax.h>
#include <std/ios/sys.h>
#include <std/lib/vector.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/crt.h>
#include <std/sys/event_fd.h>
#include <std/sys/throw.h>

#include <stdlib.h>
#include <stdio.h>
#include <langinfo.h>
#include <locale.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
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

    struct CallConfigChanged final: public Listener {
        explicit CallConfigChanged(ApplicationImpl* application);

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
        // True until the first frame supplies real metrics; -geometry is
        // applied against them exactly once.
        bool initialGeometryPending = true;
        // Set once in run(), before showWindow() reads it: whether the
        // quick-terminal window actually has a working way to be shown
        // again after it starts hidden. False on every platform without
        // the macOS hotkey module, and on macOS whenever quickHotkey
        // failed to parse or register.
        bool quickHotkeyActive = false;
        // The last window state -verbose reported. updateWindowInfo() runs
        // on every frame, so the trace is a transition and not a state:
        // without these two it would print the same line sixty times a
        // second. The grid half of the old trace moved to Composer::resize()
        // (F4, Q2), which is the only place that knows a grid changed.
        bool tracedFullscreen = false;
        bool tracedMaximized = false;

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
        void configChanged();
        void replaceFontpack(u16 size);
        void publishFontChanged();
        void wire();
    };
}

// Forward declared: ~ApplicationImpl() below calls it, but its
// definition sits with the rest of the quick-companion lifecycle code,
// next to childSignalHandler() and quickCompanionPid further down.
static void stopQuickCompanion();

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

CallConfigChanged::CallConfigChanged(ApplicationImpl* application_)
    : application(application_)
{
}

void CallConfigChanged::onListen(void*) {
    application->configChanged();
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
    composer.configChangedListeners.pushBack(composer.pool->make<CallConfigChanged>(this));
    composer.inputBindings->add(InputActions::IncFontSize, &composer.fontIncListeners);
    composer.inputBindings->add(InputActions::DecFontSize, &composer.fontDecListeners);
    composer.inputBindings->add(InputActions::ResetFontSize, &composer.fontResetListeners);
}

ApplicationImpl::~ApplicationImpl() {
    // Covers the paths where close() (and its own _exit()) never runs -
    // an exception unwinding out of run() after the companion was
    // spawned, or the SHITTY_FOR_TESTS build, where close() stops the
    // event loop instead of exiting and run() returns normally instead.
    // A no-op when close() already ran: stopQuickCompanion() clears the
    // pid on its way out.
    stopQuickCompanion();
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
    const Insets insets = composer.contentInsets();
    // One cell plus the reserve is the smallest window that still shows a
    // terminal; the bare reserve is the base a resize increment counts
    // cells from.
    const GridPixelSize smallest = gridPixelSize(1, 1, insets, composer.glyphWidth, composer.glyphHeight);
    const GridPixelSize reserve = gridPixelSize(0, 0, insets, composer.glyphWidth, composer.glyphHeight);
    composer.window->requestMinimumSize(smallest.width, smallest.height);
    composer.window->requestResizeUnit(composer.glyphWidth, composer.glyphHeight, reserve.width, reserve.height);
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
    const GridPixelSize target = gridPixelSize(columns, rows, insets, composer.glyphWidth, composer.glyphHeight);
    composer.window->requestResize(target.width, target.height);
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
    setFontSize(composer.opts->fontsize);
}

void ApplicationImpl::contentScaleChanged() {
    if (fontpackPool != nullptr) {
        try {
            replaceFontpack(composer.fontSize);
        } catch (...) {
            // The physical border is derived from contentScale independently
            // of the font resource, so its geometry still has to be applied.
            fontChanged();
        }
    }
}

void ApplicationImpl::configChanged() {
    if (fontpackPool == nullptr) {
        return;
    }
    try {
        replaceFontpack(composer.opts->fontsize);
    } catch (...) {
        // Border and geometry derive directly from the new snapshot even
        // when an external font resource cannot be reopened.
        fontChanged();
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
        if (StringView(argv[k]) != StringView(u8"--test-fd")) {
            continue;
        }
        if (k + 1 >= argc) {
            raiseError(StringView(u8"--test-fd requires a descriptor"));
        }
        i64 fd = -1;
        if (!parseI64(StringView(argv[k + 1]), fd) || fd < 0 || fd > INT_MAX) {
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

// The quick-terminal companion's pid, once spawned - not a session
// child at all, just a sibling GUI process this one starts and later
// signals. sig_atomic_t rather than pid_t: it has to be the type the
// signal handler below can read and write safely, and every pid_t this
// platform hands out fits in it. -1 means "no companion running";
// childSignalHandler reaps it like any other child (it is still this
// process's child and must not be left a zombie) but must not let its
// exit status overwrite lastChildStatus, which close() below reads to
// propagate the real shell's exit code.
static volatile sig_atomic_t quickCompanionPid = -1;

void ApplicationImpl::childSignalHandler(int signal, siginfo_t*, void*) {
    // SIGCHLD does not queue: one delivery may stand for several exited
    // children (the shell plus xdg-open helpers), so reap until drained.
    if (signal == SIGCHLD) {
        int status = 0;
        pid_t pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            if ((sig_atomic_t)(pid) == quickCompanionPid) {
                quickCompanionPid = -1;
                continue;
            }
            // Reap only. Which shell dying ends the process is not
            // decidable here: the answer depends on how many sessions are
            // left, and that races with the close this same death is
            // about to trigger through the pty's EOF path. That path owns
            // the decision; the status is recorded for it to exit with.
            lastChildStatus = (sig_atomic_t)(WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
        }
    }
}

// Kills the companion, if one is running, and forgets its pid so a
// second call (close() then the destructor, on the paths where both
// run) is a harmless no-op. SIGTERM, not SIGHUP: the companion is not a
// pty session's shell, it is a whole second copy of this program, and
// its own default disposition for SIGTERM is termination. There is no
// attempt to wait for it to actually exit - the ordinary SIGCHLD path
// above reaps it - and no respawn if it already died on its own: this
// runs once, at this process's own exit, and does not try to keep the
// companion alive.
//
// Known gap, not fixed here: a SIGKILL against this process skips this
// entirely, same as it skips every other destructor and cleanup path in
// the program. The companion is then left running until it, or its own
// shell, exits on its own.
static void stopQuickCompanion() {
    if (quickCompanionPid <= 0) {
        return;
    }
    kill((pid_t)(quickCompanionPid), SIGTERM);
    quickCompanionPid = -1;
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
    // composer.sessions, not the member: under test the session set is
    // the harness's, published there before the first frame can land.
    Vterm* const vterm = composer.sessions->activeTerminal();
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
    // A2: the frame is a list of panes even when there is one of them.
    // The production path goes through the pane form so that the form
    // the panes will arrive in is the form this code already speaks -
    // and so the throughput A3 is measured on is measured on it.
    const PaneUpdate pane = surfacePane(composer, *output);
    const bool presented = composer.renderer->update(&pane, 1);
    if (!presented) {
        composer.window->requestFrame();
        return false;
    }
    // Keep the input-method candidate window anchored to the cursor cell.
    const CellOrigin anchor = cellOrigin(output->cursor.posX, output->cursor.posY, composer.contentInsets(), composer.glyphWidth, composer.glyphHeight);
    composer.window->requestTextInputRect(anchor.x, anchor.y, composer.glyphWidth, composer.glyphHeight);
    vterm->consume();
    return true;
}

void ApplicationImpl::close() {
    stopQuickCompanion();
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
    // The grid line is Composer::resize()'s to print: a reserve set by
    // cmd+b or by a reload re-counts the grid without any platform
    // callback, and the trace that lived here missed every one of them
    // (F4, Q2). What only this callback knows is the window state the
    // full-screen transition bugs live in, so that is what stays.
    if (composer.opts->verbose && (info.fullscreen != tracedFullscreen || info.maximized != tracedMaximized)) {
        fprintf(stderr, "%s: window: %ux%u px, %s%s\n", composer.brand->identifierCString(), info.width, info.height, info.fullscreen ? "fullscreen" : "windowed", info.maximized ? ", maximized" : "");
    }
    tracedFullscreen = info.fullscreen;
    tracedMaximized = info.maximized;
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
        composer.sessions->activeTerminal()->expose();
    }
    return presentTerminal();
}

bool ApplicationImpl::eventLoop() {
    composer.platform->run();
    return true;
}

void ApplicationImpl::showWindow() {
    const Insets insets = composer.contentInsets();
    const u32 width = gridPixelWidth(composer.opts->nCols, insets, composer.glyphWidth);
    const u32 height = gridPixelHeight(composer.opts->nRows, insets, composer.glyphHeight);
    if (!composer.opts->quick || !quickHotkeyActive) {
        // A quick-terminal window with a working hotkey starts hidden;
        // nothing else shows it until the hotkey fires and toggles it via
        // requestShowAt(). Without one - unparsable or unregistered
        // quickHotkey, or simply no hotkey module outside macOS -
        // quickHotkeyActive stays false and the window shows normally
        // instead, so the shell behind it is never permanently
        // unreachable; run() already sent a diagnostic to stderr for that
        // case. The grid still needs its initial size below either way.
        composer.window->requestShow();
    }
    composer.resize((u16)(min(width, (u32)(UINT16_MAX))), (u16)(min(height, (u32)(UINT16_MAX))));
}

namespace {
    // A6: a saved frame wins over quickGeometry once one exists;
    // deleting the state file (T2's documented reset) falls back to
    // quickGeometry, unchanged from before this option existed -
    // loadQuickFrame() already treats a missing or corrupt file as
    // "nothing saved", so this only ever does anything when there
    // really is something to apply.
    void applySavedQuickFrame(Composer& composer) {
        if (!composer.opts->quickRememberFrame) {
            return;
        }
        StringBuilder path;
        if (!defaultQuickFramePath(composer.opts->configPath, path)) {
            return;
        }
        QuickFrame frame;
        if (!loadQuickFrame(StringView(path), frame)) {
            return;
        }
#if defined(__APPLE__)
        // Guarded the same way createQuickHotkey()/createCsdTabsUi() are
        // below: applyQuickFrameToWindow() is defined in the darwin-only
        // ui_quick_hotkey.mm (build.py:660), and calling it from here
        // unconditionally left every non-Apple build with an unresolved
        // symbol, reached through toggleQuickWindow() (R2-test, L1). The
        // portable fallback underneath is what those platforms run, and
        // it was always meant to be the only thing they run.
        if (applyQuickFrameToWindow(composer, frame)) {
            // The Cocoa path (ui_quick_hotkey.mm): one atomic setFrame:,
            // sidesteps the requestMove()/requestResize() ordering
            // hazard below entirely (F2's report, B2) by never using
            // those two separate calls in the first place.
            //
            // requestShowAt() has already sized the grid to the screen it
            // put the window on - the one under the pointer - and the
            // frame just applied may have moved the window back to a
            // different one. When the two screens' content scales differ,
            // the scale change that follows regrids the font and resizes
            // the window to the grid of the screen it no longer is on,
            // pulling it off the frame just restored: measured on a 1x
            // monitor plus a 2x panel, a restored 1000x500 came back
            // 980x490 without this. Re-deriving the grid from the window
            // as it now is - still expressed in the scale the composer
            // currently carries, since the scale change has not been
            // delivered yet - makes that resize reproduce the restored
            // frame instead of replacing it.
            const plt::WindowInfo restored = composer.window->info();
            composer.resize((u16)(quickFrameRegridExtent(restored.width, restored.contentScale, composer.contentScale)), (u16)(quickFrameRegridExtent(restored.height, restored.contentScale, composer.contentScale)));
            return;
        }
#endif
        // Fallback for a backend with no concrete NSWindow to reach
        // through Window::renderContext() (headless today, and any
        // future non-Cocoa backend): the portable Window interface only
        // offers requestMove()/requestResize() separately. It runs the
        // same quickFrameTarget() clamp the Cocoa path does - one
        // implementation, so the tests below stand on the real thing
        // (R2-qa round 2, I8) - but not the atomicity fix (B2), which is
        // acceptable because requestResize() is only asynchronous by
        // construction on Cocoa (see its own comment in
        // platform_cocoa.mm, guarding against re-entering frame());
        // headless applies both synchronously and has no such hazard.
        //
        // WindowInfo describes the screen by its pixel size alone, with
        // no origin and no chrome to ask about: the visible area is the
        // whole screen in points, starting at zero, and the titlebar
        // height is zero, which makes the target frame the content frame.
        const plt::WindowInfo info = composer.window->info();
        const double scale = info.contentScale > 0.0f ? (double)(info.contentScale) : 1.0;
        const QuickFrameRect visible{
            .x = 0,
            .y = 0,
            .width = (double)(max(1u, info.screenPixelWidth)) / scale,
            .height = (double)(max(1u, info.screenPixelHeight)) / scale,
        };
        const QuickFrameRect target = quickFrameTarget(frame, visible, 0);
        composer.window->requestMove((i32)(target.x), (i32)(target.y));
        composer.window->requestResize((u32)(target.width * scale), (u32)(target.height * scale));
    }
}

// The one entry point ui_quick_hotkey's Carbon handler calls on every
// press; a free function rather than a method because it is declared in
// ui_quick_hotkey.h, which the hotkey module includes without pulling in
// ApplicationImpl. Asks composer.window for its actual state instead of
// keeping a flag here: hide-on-resign-key (platform_cocoa.mm) can hide
// the window from underneath this without going through this function
// at all, and a flag of our own would then disagree with reality on the
// very next press.
//
// visible() alone is not enough: on Cocoa, a miniaturized window still
// answers isVisible with true, so a naive toggle would try to hide an
// already-Dock-hidden window instead of bringing it back. info().iconified
// catches that case and routes it through the show branch instead.
void toggleQuickWindow(Composer& composer) {
    if (composer.window == nullptr) {
        return;
    }
    const bool showing = composer.window->visible() && !composer.window->info().iconified;
    if (showing) {
        composer.window->requestHide();
    } else {
        composer.window->requestShowAt(plt::ShowPlacement::TopOfActiveScreen);
        applySavedQuickFrame(composer);
    }
}

#if defined(SHITTY_FOR_TESTS)
namespace {
    // R2-qa round 2, I10: a way to drive the quick window in a live
    // process without a global hotkey. Two rounds of acceptance testing
    // failed to deliver a single synthetic Carbon chord into a test
    // binary (0 of 22 presses with every permission granted), and the
    // one thing that did work - a marker file plus SIGUSR1 - meant
    // patching configuration.cpp by hand for every measurement. This is
    // that patch, made permanent and kept out of the shipped terminal:
    // SHITTY_FOR_TESTS is the same build flag takeTestFd() lives under,
    // so only st_test carries it.
    //
    // SIGUSR2 because SIGUSR1 is already the config reload
    // (configuration.cpp). The signal handler only bumps an eventfd the
    // poller watches, the same shape ConfigImpl uses, so the toggle
    // itself runs on the main loop - AppKit is not async-signal-safe and
    // toggleQuickWindow() reaches straight into it.
    //
    //   .build/st_test -config /tmp/q.toml &
    //   kill -USR2 $!            # show; again to hide
    EventFD* quickToggleEvent = nullptr;

    struct QuickToggleSignal final: public PollCallback {
        explicit QuickToggleSignal(Composer& composer_)
            : composer(composer_)
        {
        }

        ~QuickToggleSignal() {
            // The SIGUSR2 handler writes through quickToggleEvent, which
            // points at this object's own EventFD. Leaving it set once the
            // pool that owns this is gone left one signal between the
            // process and a write to freed memory (R2-test, Z4). Only
            // reachable in st_test, and only after run() has returned -
            // but the handler stays installed until the process exits, so
            // "unreachable in practice" is not the same as safe.
            quickToggleEvent = nullptr;
        }

        void ready(PollFD) override {
            event.drain();
            composer.platform->poller()->arm(waiter);
            toggleQuickWindow(composer);
        }

        Composer& composer;
        EventFD event;
        PollWaiter waiter;
    };

    void quickToggleSignalHandler(int) {
        if (quickToggleEvent != nullptr) {
            quickToggleEvent->signal();
        }
    }

    // Best-effort and never fatal: a test-only convenience must not be
    // able to stop the terminal from starting.
    void installQuickToggleSignal(Composer& composer) {
        QuickToggleSignal* const toggle = composer.pool->make<QuickToggleSignal>(composer);
        toggle->waiter.fd = {
            .fd = toggle->event.fd(),
            .flags = PollFlag::In,
        };
        toggle->waiter.callback = toggle;
        composer.platform->poller()->arm(toggle->waiter);
        quickToggleEvent = &toggle->event;
        struct sigaction action{};
        action.sa_handler = quickToggleSignalHandler;
        action.sa_flags = SA_RESTART;
        sigemptyset(&action.sa_mask);
        if (sigaction(SIGUSR2, &action, nullptr) < 0) {
            quickToggleEvent = nullptr;
            composer.platform->poller()->cancel(toggle->waiter);
        }
    }
}
#endif

void ApplicationImpl::checkLocale() {
    const char* locale = setlocale(LC_ALL, "");
    if (locale != nullptr && StringView(nl_langinfo(CODESET)) == StringView(u8"UTF-8")) {
        return;
    }
    // A terminal launched outside a login context - Automator, launchd,
    // a .app bundle - inherits no locale at all and lands in plain "C",
    // which turns every non-ASCII listing in the child shell into
    // question marks (issue 63). Unless the user pinned LC_ALL
    // explicitly, force a UTF-8 character type and export it so the
    // shell inherits it; the other locale categories stay untouched.
    const char* pinned = getenv("LC_ALL");
    if (pinned == nullptr || pinned[0] == '\0') {
        static const char* const candidates[] = {"C.UTF-8", "UTF-8", "en_US.UTF-8"};
        for (const char* candidate : candidates) {
            if (setlocale(LC_CTYPE, candidate) == nullptr) {
                continue;
            }
            if (StringView(nl_langinfo(CODESET)) == StringView(u8"UTF-8")) {
                setenv("LC_CTYPE", candidate, 1);
                return;
            }
        }
    }
    if (locale == nullptr) {
        sysO << StringView(u8"Warning: could not set locale; international input may be broken.") << endL;
        return;
    }
    sysO << StringView(u8"Warning: non-UTF-8 locale ") << StringView(locale) << StringView(u8"; international input may be broken.") << endL;
}

int ApplicationImpl::run(int argc, char* argv[]) {
    // Captured before anything below can touch argv: takeTestFd() and
    // Config::initialize() may both shift later entries out from under
    // consumed flags, but neither ever moves argv[0] itself. This is
    // what a spawned quick-terminal companion re-execs.
    const char* const argv0 = argv[0];
    int testFd = -1;
#ifdef SHITTY_FOR_TESTS
    testFd = takeTestFd(argc, argv);
#endif
    checkLocale();
    // After the locale: option parsing resolves the auto width level by
    // probing the libc's wcwidth.
    composer.config = Config::create(composer);
    composer.config->initialize(&argc, argv);
    // In the parent, before any thread exists. TERM and the version are
    // process-wide constants identical for every terminal behind the
    // window, and setenv() must never run in a forked child of a
    // multithreaded process: glibc's environ lock is not reset at fork.
    configureTerminalChildEnvironment(*composer.brand, composer.opts->widths);
    composer.fontSize = composer.opts->fontsize;
    composer.inputRemap = InputRemap::create(composer);
    if (testFd >= 0) {
        return runTestMode(composer, *TestInput::create(composer), *this, *this, testFd, argc, argv);
    }

    composer.launch = composer.pool->make<LaunchCommand>(buildLaunchCommand(argc, argv, composer.opts->shell, composer.opts->login));
    if (composer.platform == nullptr) {
        composer.platform = plt::Platform::create(*composer.pool);
    }
    // Input deliveries run on one fiber, so stream-backed handlers may
    // suspend without stopping the event loop; later input waits in the
    // sink's queue.
    composer.input = plt::createFiberInputSink(*composer.pool, *composer.platform->scheduler(), *composer.input);
    composer.window = composer.platform->createWindow(
        *composer.pool,
        {
            .appId = composer.brand->identifier(),
            .title = composer.opts->title,
            .width = (u32)(max(320, (int)(composer.opts->nCols) * composer.opts->fontsize / 2)),
            .height = (u32)(max(200, (int)(composer.opts->nRows) * composer.opts->fontsize)),
            .decorations = !composer.opts->noDecorations,
            .transparentTitlebar = composer.opts->transparentTitlebar,
            .quick = composer.opts->quick,
            .quickGeometry = composer.opts->quickGeometry,
            .quickCornerRadius = composer.opts->quickCornerRadius,
            .input = composer.input,
            .events = this,
            .frame = this,
            .drop = createDropTarget(*composer.pool, composer),
            .icon = composer.brand->iconData(),
            .appName = composer.brand->displayName(),
        }
    );
#if defined(__APPLE__)
    // The title-bar tab strip: a fire-and-forget listener over the
    // NSWindow the render context carries.
    createCsdTabsUi(*composer.pool, composer);
    // The sidebar tab list, the same way, and inside the same guard for
    // the same reason: ui_sidebar_tabs.mm is in the darwin sources only
    // (build.py), and calling into it unconditionally is exactly what
    // left every non-Apple build with an unresolved symbol last time
    // (R2-test, L1). It is created whether or not -sidebarTabs is on -
    // the object reserves nothing and draws nothing while the option is
    // off, and a config reload can turn it on without a restart.
    createSidebarTabsUi(*composer.pool, composer);
    if (composer.opts->quick) {
        // The global hotkey that shows and hides the quick-terminal
        // window; wired up only when the window is actually behaving as
        // one, so a plain terminal never claims the chord from the rest
        // of the system.
        quickHotkeyActive = createQuickHotkey(*composer.pool, composer);
    }
#endif
    if (composer.opts->quick && !quickHotkeyActive) {
        // No working hotkey - either this build has no hotkey module at
        // all (every non-Apple platform today), or createQuickHotkey()
        // already printed why the chord itself didn't take. Either way
        // the window cannot stay hidden forever with nothing able to
        // bring it back: showWindow() reads quickHotkeyActive and shows
        // it normally below.
        sysE << composer.brand->identifier() << StringView(u8": quick: no working hotkey to bring the window back with; showing it normally instead") << endL;
    }
    composer.config->start();
    STD_DEFER {
        composer.config->stop();
    };
    contentScaleChanged();

    replaceFontpack(composer.opts->fontsize);
    if (composer.opts->maximized) {
        composer.window->requestMaximized(true);
    }
    showWindow();

    setupSignals();
#if defined(SHITTY_FOR_TESTS)
    // After setupSignals() for the same reason the companion spawn below
    // is: this installs a disposition of its own and must not be undone
    // by the handler setup that follows it.
    installQuickToggleSignal(composer);
#endif
    // After setupSignals(): the custom SIGCHLD handler has to be in
    // place before the companion can die, or its exit would be reaped
    // silently under the still-default disposition and never clear
    // quickCompanionPid above. Every guard - the option unset, this
    // being a quick window itself, a self-referential path, a fork/exec
    // failure - lives in spawnQuickCompanion() and leaves this process
    // running either way; only a real pid is ever stored.
    quickCompanionPid = (sig_atomic_t)(spawnQuickCompanion(composer.opts->quickCompanion, composer.opts->configPath, composer.opts->quick, argv0, composer.brand->identifier()));
    composer.pty = createPty(*composer.pool, *composer.platform->scheduler(), composer.platform);

    createRenderer();
    SessionSet::create(composer);

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
