/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "ui_quick_hotkey.h"

#include "brand.h"
#include "composer.h"
#include "options.h"
#include "quick_hotkey_chord.h"

#include <std/ios/sys.h>
#include <std/mem/obj_pool.h>
#include <std/str/view.h>
#include <std/sys/types.h>

#include <Carbon/Carbon.h>

using namespace stl;

namespace {
    // Registers Options::quickHotkey as a Carbon global hotkey for the
    // lifetime of the pool it lives in, and hands every press to
    // toggleQuickWindow(). A pool object like createCsdTabsUi's
    // CsdTabsUi: fire-and-forget, torn down by its owning ObjPool's
    // destructor (RegisterEventHotKey's own docs say the registration
    // itself needs no explicit cleanup at process exit - "the system
    // will take care of that for you" - but unregistering explicitly
    // here is what makes a hypothetical future reconfiguration, or any
    // path that destroys this object while the process keeps running,
    // safe rather than relying on that fallback).
    struct QuickHotkeyUi {
        explicit QuickHotkeyUi(Composer& composer);
        ~QuickHotkeyUi();

        Composer& composer;
        EventHotKeyRef hotkeyRef = nullptr;
        EventHandlerRef handlerRef = nullptr;
        // True only once both the handler and the hotkey itself are
        // actually registered; createQuickHotkey() reports this back so
        // the caller can show the window normally instead of leaving it
        // unreachable when it stays false.
        bool active = false;
    };

    static OSStatus quickHotkeyPressed(EventHandlerCallRef, EventRef, void* userData) {
        QuickHotkeyUi* const self = (QuickHotkeyUi*)(userData);
        toggleQuickWindow(self->composer);
        return noErr;
    }
}

QuickHotkeyUi::QuickHotkeyUi(Composer& composer_)
    : composer(composer_)
{
    u32 modifiers = 0;
    u32 keyCode = 0;
    if (!parseQuickHotkey(composer.opts->quickHotkey, modifiers, keyCode)) {
        sysE << composer.brand->identifier() << StringView(u8": quickHotkey: unrecognized chord '") << composer.opts->quickHotkey << StringView(u8"'; the quick-terminal hotkey is disabled") << endL;
        return;
    }
    if (modifiers == 0) {
        // A bare key with no modifier would grab that key system-wide -
        // every application, every text field, for as long as this
        // process runs. RegisterEventHotKey accepts it without complaint
        // (verified: it returns noErr the same as any other chord), so
        // this has to be rejected here instead of relied on to fail.
        sysE << composer.brand->identifier() << StringView(u8": quickHotkey: '") << composer.opts->quickHotkey << StringView(u8"' has no modifier (ctrl/shift/alt/super); the quick-terminal hotkey is disabled") << endL;
        return;
    }
    const EventTypeSpec pressed = {kEventClassKeyboard, kEventHotKeyPressed};
    if (InstallEventHandler(GetApplicationEventTarget(), quickHotkeyPressed, 1, &pressed, this, &handlerRef) != noErr) {
        sysE << composer.brand->identifier() << StringView(u8": quickHotkey: could not install the event handler; the quick-terminal hotkey is disabled") << endL;
        handlerRef = nullptr;
        return;
    }
    const EventHotKeyID hotkeyId = {(OSType)(1), 1};
    if (RegisterEventHotKey(keyCode, modifiers, hotkeyId, GetApplicationEventTarget(), 0, &hotkeyRef) != noErr) {
        // Not a same-chord conflict: Carbon lets multiple processes (and
        // even this chord already held elsewhere) register the very same
        // combination without complaint (verified). A real refusal here
        // has some other cause the system does not report back.
        sysE << composer.brand->identifier() << StringView(u8": quickHotkey: the system refused to register '") << composer.opts->quickHotkey << StringView(u8"'; the quick-terminal hotkey is disabled") << endL;
        RemoveEventHandler(handlerRef);
        handlerRef = nullptr;
        hotkeyRef = nullptr;
        return;
    }
    active = true;
}

QuickHotkeyUi::~QuickHotkeyUi() {
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
