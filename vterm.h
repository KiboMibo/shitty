/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

/* This file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the file LICENSE.GPL3 for the full license.
 */

#pragma once
#include <std/sys/types.h>

#include "terminal_types.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <sys/types.h>

struct Composer;
struct Pty;
struct VtermHost;
struct VtermTrace;

enum class VtKey {
    NONE,
    Space,
    Return,
    Backspace,
    Tab,
    Backtick,
    Tilde,
    Up,
    Down,
    Left,
    Right,
    Insert,
    Delete,
    Home,
    End,
    PageUp,
    PageDown,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    F13,
    F14,
    F15,
    F16,
    F17,
    F18,
    F19,
    F20,
    K0,
    K1,
    K2,
    K3,
    K4,
    K5,
    K6,
    K7,
    K8,
    K9,
    KP_F1,
    KP_F2,
    KP_F3,
    KP_F4,
    KP_Insert,
    KP_Delete,
    KP_Up,
    KP_Down,
    KP_Left,
    KP_Right,
    KP_Home,
    KP_End,
    KP_PageUp,
    KP_PageDown,
    KP_Begin,
    KP_Plus,
    KP_Minus,
    KP_Star,
    KP_Slash,
    KP_Comma,
    KP_Dot,
    KP_Space,
    KP_Equal,
    KP_Tab,
    KP_Enter,
    KP_0,
    KP_1,
    KP_2,
    KP_3,
    KP_4,
    KP_5,
    KP_6,
    KP_7,
    KP_8,
    KP_9,
    CapsLock,
    ScrollLock,
    NumLock,
    Pause,
    Menu,
    Print,
    LeftShift,
    LeftControl,
    LeftAlt,
    LeftSuper,
    RightShift,
    RightControl,
    RightAlt,
    RightSuper
};

enum class VtModifier : u8 {
    none = 0,
    shift = 1,
    control = 2,
    shift_control = 3,
    alt = 4,
    shift_alt = 5,
    control_alt = 6,
    shift_control_alt = 7
};

constexpr VtModifier operator|(VtModifier lhs, VtModifier rhs) {
    return (VtModifier)((u8)(lhs) | (u8)(rhs));
}

constexpr VtModifier operator&(VtModifier lhs, VtModifier rhs) {
    return (VtModifier)((u8)(lhs) & (u8)(rhs));
}

enum class MouseTrackingMode : u8 {
    Disabled = 0,
    X10_Compat,
    VT200,
    VT200_ButtonEvent,
    VT200_AnyEvent,
    VT200_Highlight
};
enum class MouseTrackingEnc : u8 {
    Default = 0,
    UTF8,
    SGR,
    URXVT,
    SGRPixels
};

struct MouseTrackingState {
    MouseTrackingMode mode = MouseTrackingMode::Disabled;
    MouseTrackingEnc enc = MouseTrackingEnc::Default;
    bool focusEventMode = false;
    u32 generation = 0;

    void setMode(MouseTrackingMode value);
    void setEncoding(MouseTrackingEnc value);
};

struct RectangleOrigin {
    u16 rowBase;
    u16 columnBase;
    u16 rowLimit;
    u16 columnLimit;
};

struct Vterm {
    enum class KeyEventType : u8 {
        Press = 1,
        Repeat = 2,
        Release = 3
    };

    virtual bool getScreenReverseVideo() const = 0;
    virtual u8 getLedState() const = 0;
    virtual bool getReverseWrapMode() const = 0;
    virtual bool getNationalReplacementMode() const = 0;
    virtual bool getMetaMode() const = 0;
    virtual bool getAnsiMode(u32 mode) const = 0;
    virtual bool getPrivateMode(u32 mode) const = 0;
    virtual TerminalCursor::Style getCursorStyle() const = 0;
    virtual TerminalPen getPenState() const = 0;
    virtual RectangleOrigin getRectangleOrigin() const = 0;

    virtual void resize(u16 winPx, u16 winPy) = 0;
    virtual void redraw() = 0;
    virtual bool synchronizedOutputActive() const = 0;
    virtual bool expireSynchronizedOutput(bool force = false) = 0;
    virtual bool animationActive() const = 0;
    virtual bool advanceAnimation(bool force = false) = 0;

    virtual int writePty(VtKey key, VtModifier modifiers = VtModifier::none, bool userInput = false) = 0;
    virtual int writePty(u8 ch, VtModifier modifiers = VtModifier::none, bool userInput = false) = 0;
    virtual int writePty(const char* cstr, bool userInput = false) = 0;
    virtual int writePty(const u8* data, size_t size, bool userInput = false) = 0;
    virtual bool flushPtyOutput() = 0;
    virtual bool hasPendingPtyOutput() const = 0;
    virtual size_t pendingPtyOutputBytes() const = 0;
    virtual int writeKittyKey(VtKey key, u16 modifiers, KeyEventType event) = 0;
    virtual int writeKittyKey(u32 key, u32 shiftedKey, u32 baseLayoutKey, u16 modifiers, KeyEventType event) = 0;
    virtual u8 getKittyKeyboardFlags() const = 0;

    virtual bool readPty() = 0;
    virtual bool servicePty(bool readable, bool writable) = 0;
    virtual void feedPtyOutput(const u8* data, size_t size) = 0;
    virtual void setParserTrace(VtermTrace* trace) = 0;

    virtual const MouseTrackingState& getMouseTrackingState() const = 0;
    virtual bool mouseHighlightRelease(u16 endX, u16 endY, u16 mouseX, u16 mouseY) = 0;
    virtual void setLocatorPosition(u16 column, u16 row, u16 pixelX, u16 pixelY, u8 buttons = 0) = 0;
    virtual void reportLocatorButton(u8 button, bool pressed) = 0;

    virtual void setHasFocus(bool focused) = 0;
    virtual void setHyperlink(const std::string& parametersAndUri) = 0;
    virtual std::string getHyperlink(int pixelX, int pixelY) const = 0;
    virtual size_t getHyperlinkCount() const = 0;
    virtual void mouseWheelUp(u16 count = 1) = 0;
    virtual void mouseWheelDown(u16 count = 1) = 0;
    virtual void pageUp() = 0;
    virtual void pageDown() = 0;

    virtual void selectStart(int pixelX, int pixelY, bool cycleSnapTo) = 0;
    virtual void selectExtend(int pixelX, int pixelY, bool cycleSnapTo) = 0;
    virtual void selectUpdate(int pixelX, int pixelY) = 0;
    virtual bool selectFinish(std::string& selection) = 0;
    virtual void selectClear() = 0;
    virtual void selectRectangularModeToggle() = 0;
    virtual void pasteSelection(const std::string& selection) = 0;

    static Vterm* create(Composer& composer, VtermHost& host, Pty& pty, u16 glyphPx, u16 glyphPy, u16 winPx, u16 winPy);
};
