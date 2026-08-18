/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "ui_quick_hotkey.h"

#include "brand.h"
#include "composer.h"
#include "options.h"
#include "quick_frame_store.h"
#include "quick_hotkey_chord.h"

#include <plt/window.h>

#include <std/alg/minmax.h>
#include <std/ios/sys.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/types.h>

// Same guard ui_csd_tabs.mm uses: AppKit pulls in the legacy QuickDraw
// Point/Rect typedefs transitively, which would collide with
// lib/shitty/point.h and rect.h if this file ever needs them through
// composer.h - it does not today, but there is no reason for this file
// to be the one exception to a precedent it otherwise follows closely.
#define Point MacLegacyPoint
#define Rect MacLegacyRect

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>

#undef Rect
#undef Point

using namespace stl;

namespace {
    // B4's write-back guard. A file-scope pair because there is exactly
    // one quick window per process - the Carbon hotkey, the resign-key
    // observer and applyQuickFrameToWindow() all already build on that.
    //
    // applyQuickFrameToWindow() leaves here the frame it had to squeeze
    // into a screen the saved one did not fit, so the resign-key
    // observer can tell "the user put the window here" from "this code
    // guessed". Persisting the guess is what turned one mis-resolved
    // screen into permanent data loss: each show moved the stored frame
    // further from what the user had set, with no way back but deleting
    // the state file (R2-qa round 2, B4).
    NSRect clampedQuickFrame = NSZeroRect;
    bool haveClampedQuickFrame = false;

    // Registers Options::quickHotkey (and, best-effort, quickFullscreenHotkey)
    // as Carbon global hotkeys for the lifetime of the pool it lives in,
    // and hands every press to toggleQuickWindow() or the fullscreen
    // toggle below. Also observes the window resigning key status to
    // persist a dragged/resized frame (quickRememberFrame). A pool
    // object like createCsdTabsUi's CsdTabsUi: fire-and-forget, torn
    // down by its owning ObjPool's destructor (RegisterEventHotKey's own
    // docs say the registration itself needs no explicit cleanup at
    // process exit - "the system will take care of that for you" - but
    // unregistering explicitly here is what makes a hypothetical future
    // reconfiguration, or any path that destroys this object while the
    // process keeps running, safe rather than relying on that fallback).
    struct QuickHotkeyUi {
        explicit QuickHotkeyUi(Composer& composer);
        ~QuickHotkeyUi();

        // Shared by both hotkeys: parses chord, rejects a bare key with
        // no modifier (it would grab that key system-wide), registers
        // it under id, and reports every failure through name/disabledText.
        // Returns whether it ended up registered.
        bool registerHotkey(StringView chord, EventHotKeyID id, EventHotKeyRef& outRef, const char* name, const char* disabledText);

        Composer& composer;
        EventHotKeyRef hotkeyRef = nullptr;
        EventHotKeyRef fullscreenHotkeyRef = nullptr;
        EventHandlerRef handlerRef = nullptr;
        // True only once both the handler and quickHotkey itself are
        // actually registered; createQuickHotkey() reports this back so
        // the caller can show the window normally instead of leaving it
        // unreachable when it stays false. Independent of
        // fullscreenHotkeyRef - a broken quickFullscreenHotkey never
        // affects this.
        bool active = false;
        // The frame from just before the fullscreen chord last expanded
        // the window, so the same chord collapses it back exactly.
        // Purely in-memory and unrelated to quickRememberFrame's on-disk
        // state (quick_frame_store.h): that persists a user's manually
        // dragged/resized frame across shows; this is an ephemeral undo
        // point for one toggle.
        NSRect priorFullscreenFrame = NSZeroRect;
        // The frame actually reached last time this expanded the window -
        // read back right after -setFrame:, not recomputed from
        // screen.visibleFrame on every press. A titled window's frame
        // can differ from what was requested (AppKit keeps it clear of
        // the menu bar), so comparing against the live screen rect made
        // the fold-back branch permanently unreachable (F2's report, B3).
        NSRect appliedFullscreenFrame = NSZeroRect;
        bool hasPriorFullscreenFrame = false;
        // NSWindowDidResignKeyNotification observer for quickRememberFrame;
        // nil when the option is off or the window has no native handle
        // yet. Independent of both hotkeys - registered even if neither
        // chord parsed, since it needs no Carbon hotkey to work, only
        // the option.
        id<NSObject> resignKeyObserver = nil;
    };

    static void toggleQuickWindowFullscreen(QuickHotkeyUi& self) {
        if (self.composer.window == nullptr || !self.composer.window->visible() || self.composer.window->info().iconified) {
            return;
        }
        const plt::RenderContext context = self.composer.window->renderContext();
        // See applyQuickFrameToWindow()'s own comment below: the backend
        // tag has to be checked before the bridge cast, not just
        // nullness - every backend hands back a non-null .window, and on
        // a non-Cocoa one it does not point at an NSWindow at all. This
        // function is only ever reached from the Carbon hotkey handler
        // above, which only exists in this Cocoa-only .mm file, but the
        // guard costs nothing and matches the sibling function exactly.
        if (context.backend != plt::RenderBackend::Cocoa) {
            return;
        }
        NSWindow* const window = (__bridge NSWindow*)(context.window);
        if (window == nil) {
            return;
        }
        NSScreen* const screen = window.screen != nil ? window.screen : [NSScreen mainScreen];
        if (screen == nil) {
            return;
        }
        if (self.hasPriorFullscreenFrame && NSEqualRects(window.frame, self.appliedFullscreenFrame)) {
            [window setFrame:self.priorFullscreenFrame display:YES animate:YES];
            // Restored after the frame shrinks back, matching "corners
            // are round exactly while not covering the whole screen".
            self.composer.window->requestCornerRadius(self.composer.opts->quickCornerRadius);
            self.hasPriorFullscreenFrame = false;
            return;
        }
        self.priorFullscreenFrame = window.frame;
        self.hasPriorFullscreenFrame = true;
        // Squared off before the frame grows, so a "fullscreen" window
        // never shows desktop through masked corners even for one
        // transient frame.
        self.composer.window->requestCornerRadius(0);
        // visibleFrame, not frame: matches quickGeometry's own convention
        // (topOfActiveScreenFrame(), platform_cocoa.mm, also visibleFrame)
        // and is the region a titled window can actually occupy without
        // AppKit silently keeping it clear of the menu bar strip (measured:
        // a titled window's resulting frame lands ~30pt short of
        // screen.frame's own height - the mismatch that made the
        // window.frame == screen.frame comparison here never true).
        [window setFrame:screen.visibleFrame display:YES animate:YES];
        self.appliedFullscreenFrame = window.frame;
    }

    static OSStatus quickHotkeyPressed(EventHandlerCallRef, EventRef event, void* userData) {
        QuickHotkeyUi* const self = (QuickHotkeyUi*)(userData);
        EventHotKeyID pressedId{};
        if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(pressedId), nullptr, &pressedId) == noErr && pressedId.id == 2) {
            toggleQuickWindowFullscreen(*self);
            return noErr;
        }
        toggleQuickWindow(self->composer);
        return noErr;
    }
}

bool QuickHotkeyUi::registerHotkey(StringView chord, EventHotKeyID id, EventHotKeyRef& outRef, const char* name, const char* disabledText) {
    u32 modifiers = 0;
    u32 keyCode = 0;
    if (!parseQuickHotkey(chord, modifiers, keyCode)) {
        sysE << composer.brand->identifier() << StringView(u8": ") << StringView(name) << StringView(u8": unrecognized chord '") << chord << StringView(u8"'; ") << StringView(disabledText) << StringView(u8" is disabled") << endL;
        return false;
    }
    if (modifiers == 0) {
        // A bare key with no modifier would grab that key system-wide -
        // every application, every text field, for as long as this
        // process runs. RegisterEventHotKey accepts it without complaint
        // (verified: it returns noErr the same as any other chord), so
        // this has to be rejected here instead of relied on to fail.
        sysE << composer.brand->identifier() << StringView(u8": ") << StringView(name) << StringView(u8": '") << chord << StringView(u8"' has no modifier (ctrl/shift/alt/super); ") << StringView(disabledText) << StringView(u8" is disabled") << endL;
        return false;
    }
    if (RegisterEventHotKey(keyCode, modifiers, id, GetApplicationEventTarget(), 0, &outRef) != noErr) {
        // Not a same-chord conflict: Carbon lets multiple processes (and
        // even this chord already held elsewhere) register the very same
        // combination without complaint (verified). A real refusal here
        // has some other cause the system does not report back.
        sysE << composer.brand->identifier() << StringView(u8": ") << StringView(name) << StringView(u8": the system refused to register '") << chord << StringView(u8"'; ") << StringView(disabledText) << StringView(u8" is disabled") << endL;
        outRef = nullptr;
        return false;
    }
    return true;
}

QuickHotkeyUi::QuickHotkeyUi(Composer& composer_)
    : composer(composer_)
{
    const EventTypeSpec pressed = {kEventClassKeyboard, kEventHotKeyPressed};
    if (InstallEventHandler(GetApplicationEventTarget(), quickHotkeyPressed, 1, &pressed, this, &handlerRef) != noErr) {
        sysE << composer.brand->identifier() << StringView(u8": quickHotkey: could not install the event handler; the quick-terminal hotkey is disabled") << endL;
        handlerRef = nullptr;
    } else {
        active = registerHotkey(composer.opts->quickHotkey, EventHotKeyID{(OSType)(1), 1}, hotkeyRef, "quickHotkey", "the quick-terminal hotkey");
        // Empty means disabled (Options only stores and never requires
        // this one non-empty, unlike quickHotkey above) - no diagnostic
        // for that case, same as quickCompanion's own "no value" shape.
        if (!composer.opts->quickFullscreenHotkey.empty()) {
            registerHotkey(composer.opts->quickFullscreenHotkey, EventHotKeyID{(OSType)(1), 2}, fullscreenHotkeyRef, "quickFullscreenHotkey", "the quick-terminal fullscreen hotkey");
        }
        if (!active && fullscreenHotkeyRef == nullptr) {
            // Neither chord took - nothing left listening on this handler.
            RemoveEventHandler(handlerRef);
            handlerRef = nullptr;
        }
    }

    if (composer.opts->quickRememberFrame && composer.window != nullptr && composer.window->renderContext().backend == plt::RenderBackend::Cocoa) {
        NSWindow* const window = (__bridge NSWindow*)(composer.window->renderContext().window);
        if (window != nil) {
            QuickHotkeyUi* const self = this;
            // Fires for both hide paths - the explicit hotkey
            // (toggleQuickWindow, application.cpp) and hide-on-resign-key
            // (WindowImpl::focused, platform_cocoa.mm) - since a key
            // window resigns key as part of being ordered out either
            // way. The frame is valid to read regardless of exactly when
            // orderOut: runs relative to this notification: hiding never
            // changes window.frame. A local block variable, rather than
            // one inline in the call below, keeps the four-keyword
            // selector on one short line instead of clang-format
            // spreading it across a deeply indented column.
            void (^saveFrame)(NSNotification*) = ^(NSNotification*) {
              if (self->hasPriorFullscreenFrame) {
                  // The fullscreen chord's own frame is not a manual
                  // placement - the option is documented as remembering
                  // what the user "manually set" (shitty.toml). Saving it
                  // here would silently overwrite the user's actual
                  // dragged/resized frame the next time the window hides
                  // while still expanded (F2's report, I2).
                  return;
              }
              if (haveClampedQuickFrame && NSEqualRects(window.frame, clampedQuickFrame)) {
                  // Untouched since applyQuickFrameToWindow() clamped it:
                  // an approximation this code produced, not a placement
                  // the user chose. Writing it back would replace the
                  // saved frame with the approximation for good (B4).
                  return;
              }
              StringBuilder path;
              if (!defaultQuickFramePath(self->composer.opts->configPath, path)) {
                  return;
              }
              // Points on both axes and both sizes (quick_frame_store.h):
              // the frame's own origin, and the content rect the frame
              // encloses, asked of this very window rather than derived
              // from a scale factor that means something different on
              // the next display (B4).
              const NSRect content = [window contentRectForFrameRect:window.frame];
              const QuickFrame frame{
                  .x = (i32)(window.frame.origin.x),
                  .y = (i32)(window.frame.origin.y),
                  .width = (u32)(max(1.0, content.size.width)),
                  .height = (u32)(max(1.0, content.size.height)),
              };
              if (!saveQuickFrame(StringView(path), frame)) {
                  // Best-effort by design (quick_frame_store.h) but not
                  // silent: a directory that does not exist or is not
                  // writable otherwise leaves quickRememberFrame looking
                  // broken with no diagnostic at all (F2's report, I3).
                  sysE << self->composer.brand->identifier() << StringView(u8": quickRememberFrame: could not save the window frame to ") << StringView(path) << endL;
              }
            };
            resignKeyObserver = [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowDidResignKeyNotification object:window queue:nil usingBlock:saveFrame];
        }
    }
}

QuickHotkeyUi::~QuickHotkeyUi() {
    if (resignKeyObserver != nil) {
        [[NSNotificationCenter defaultCenter] removeObserver:resignKeyObserver];
    }
    if (fullscreenHotkeyRef != nullptr) {
        UnregisterEventHotKey(fullscreenHotkeyRef);
    }
    if (hotkeyRef != nullptr) {
        UnregisterEventHotKey(hotkeyRef);
    }
    if (handlerRef != nullptr) {
        RemoveEventHandler(handlerRef);
    }
}

bool createQuickHotkey(ObjPool& owner, Composer& composer) {
    return owner.make<QuickHotkeyUi>(composer)->active;
}

bool applyQuickFrameToWindow(Composer& composer, const QuickFrame& frame) {
    if (composer.window == nullptr) {
        return false;
    }
    const plt::RenderContext context = composer.window->renderContext();
    // Every backend hands back a non-null .window - headless points it
    // at its own internal render target, not an NSWindow at all - so the
    // backend tag has to be checked before the bridge cast runs, not
    // just nullness (this crashed the headless regression tests: the
    // pointer bridged fine, sending it any Objective-C message did not).
    if (context.backend != plt::RenderBackend::Cocoa) {
        return false;
    }
    NSWindow* const window = (__bridge NSWindow*)(context.window);
    if (window == nil) {
        return false;
    }

    // The screen the frame was saved on, identified by the frame's own
    // origin - not window.screen, which by now answers about the screen
    // requestShowAt() just placed the window on, i.e. the one under the
    // pointer (WindowImpl::topOfActiveScreenFrame, platform_cocoa.mm).
    // Restoring against the pointer's screen is what let a frame cross
    // displays and come back clamped into a screen it was never on
    // (R2-qa round 2, B4).
    NSScreen* screen = nil;
    const NSPoint origin = NSMakePoint((CGFloat)(frame.x), (CGFloat)(frame.y));
    for (NSScreen* const candidate in [NSScreen screens]) {
        if (NSPointInRect(origin, candidate.frame)) {
            screen = candidate;
            break;
        }
    }
    if (screen == nil) {
        // Saved on a display that is not attached any more: nothing
        // better than the window's current screen is knowable, and the
        // clamp below is what makes the window reachable there at all.
        // The frame it produces is explicitly not written back.
        screen = window.screen != nil ? window.screen : [NSScreen mainScreen];
    }
    if (screen == nil) {
        return false;
    }

    // What the saved content size costs once the window's own chrome is
    // around it, asked of this window rather than assumed: the titlebar
    // is part of what has to fit the screen (R2-qa round 2, Z2). Only
    // the height differs - -frameRectForContentRect: never widens a
    // frame for any style mask this window can carry.
    const CGFloat probeSize = 100;
    const NSRect probe = [window frameRectForContentRect:NSMakeRect(0, 0, probeSize, probeSize)];
    const QuickFrameRect visible{
        .x = screen.visibleFrame.origin.x,
        .y = screen.visibleFrame.origin.y,
        .width = screen.visibleFrame.size.width,
        .height = screen.visibleFrame.size.height,
    };
    const QuickFrameTarget target = quickFrameTarget(frame, visible, probe.size.height - probeSize);

    // One atomic call, not requestMove()+requestResize(): the latter set
    // the origin against the window's still-old size (requestResize() is
    // asynchronous by design, see its own comment in platform_cocoa.mm),
    // then let -setContentSize: hold the top edge while the bottom moved -
    // drifting the saved position by the height delta on every show
    // (F2's report: measured -80pt/cycle). display:YES applies this
    // immediately instead of deferring like requestResize() does.
    [window setFrame:NSMakeRect(target.frame.x, target.frame.y, target.frame.width, target.frame.height) display:YES animate:NO];
    // Read back rather than recomputed, the same way the fullscreen
    // toggle above learned to (F2's report, B3): AppKit may settle on a
    // frame slightly different from the requested one, and the
    // resign-key observer compares against what the window actually has.
    clampedQuickFrame = window.frame;
    haveClampedQuickFrame = target.clamped;
    return true;
}
