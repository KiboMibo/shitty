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

#include <std/ios/sys.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/sys/types.h>

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>

using namespace stl;

namespace {
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
        // it under id, and reports every failure through name. Returns
        // whether it ended up registered.
        bool registerHotkey(StringView chord, EventHotKeyID id, EventHotKeyRef& outRef, const char* name);

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
        // point for one toggle. Consumed (hasPriorFullscreenFrame reset)
        // on every collapse, so a quickGeometry that happens to already
        // match the screen's own size is never mistaken for "expanded
        // by us".
        NSRect priorFullscreenFrame = NSZeroRect;
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
        NSWindow* const window = (__bridge NSWindow*)(self.composer.window->renderContext().window);
        if (window == nil) {
            return;
        }
        NSScreen* const screen = window.screen != nil ? window.screen : [NSScreen mainScreen];
        if (screen == nil) {
            return;
        }
        // Self-correcting rather than trusting hasPriorFullscreenFrame
        // across an intervening hide/show cycle (explicit hotkey or
        // hide-on-resign-key, neither of which notifies this state):
        // "currently expanded" is whatever the frame equals the
        // screen's own, checked fresh on every press, not a flag that
        // could go stale.
        if (self.hasPriorFullscreenFrame && NSEqualRects(window.frame, screen.frame)) {
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
        [window setFrame:screen.frame display:YES animate:YES];
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

bool QuickHotkeyUi::registerHotkey(StringView chord, EventHotKeyID id, EventHotKeyRef& outRef, const char* name) {
    u32 modifiers = 0;
    u32 keyCode = 0;
    if (!parseQuickHotkey(chord, modifiers, keyCode)) {
        sysE << composer.brand->identifier() << StringView(u8": ") << StringView(name) << StringView(u8": unrecognized chord '") << chord << StringView(u8"'; the hotkey is disabled") << endL;
        return false;
    }
    if (modifiers == 0) {
        // A bare key with no modifier would grab that key system-wide -
        // every application, every text field, for as long as this
        // process runs. RegisterEventHotKey accepts it without complaint
        // (verified: it returns noErr the same as any other chord), so
        // this has to be rejected here instead of relied on to fail.
        sysE << composer.brand->identifier() << StringView(u8": ") << StringView(name) << StringView(u8": '") << chord << StringView(u8"' has no modifier (ctrl/shift/alt/super); the hotkey is disabled") << endL;
        return false;
    }
    if (RegisterEventHotKey(keyCode, modifiers, id, GetApplicationEventTarget(), 0, &outRef) != noErr) {
        // Not a same-chord conflict: Carbon lets multiple processes (and
        // even this chord already held elsewhere) register the very same
        // combination without complaint (verified). A real refusal here
        // has some other cause the system does not report back.
        sysE << composer.brand->identifier() << StringView(u8": ") << StringView(name) << StringView(u8": the system refused to register '") << chord << StringView(u8"'; the hotkey is disabled") << endL;
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
        active = registerHotkey(composer.opts->quickHotkey, EventHotKeyID{(OSType)(1), 1}, hotkeyRef, "quickHotkey");
        // Empty means disabled (Options only stores and never requires
        // this one non-empty, unlike quickHotkey above) - no diagnostic
        // for that case, same as quickCompanion's own "no value" shape.
        if (!composer.opts->quickFullscreenHotkey.empty()) {
            registerHotkey(composer.opts->quickFullscreenHotkey, EventHotKeyID{(OSType)(1), 2}, fullscreenHotkeyRef, "quickFullscreenHotkey");
        }
        if (!active && fullscreenHotkeyRef == nullptr) {
            // Neither chord took - nothing left listening on this handler.
            RemoveEventHandler(handlerRef);
            handlerRef = nullptr;
        }
    }

    if (composer.opts->quickRememberFrame && composer.window != nullptr) {
        NSWindow* const window = (__bridge NSWindow*)(composer.window->renderContext().window);
        if (window != nil) {
            Composer* const target = &composer;
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
              StringBuilder path;
              if (!defaultQuickFramePath(target->opts->configPath, path)) {
                  return;
              }
              const plt::WindowInfo info = target->window->info();
              const QuickFrame frame{.x = info.x, .y = info.y, .width = info.width, .height = info.height};
              saveQuickFrame(StringView(path), frame);
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
