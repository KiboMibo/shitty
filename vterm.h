/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "terminal_types.h"

#include <std/str/view.h>
#include <std/sys/types.h>

#include <cstddef>
#include <cstdint>

struct Composer;
struct CellExtraStore;
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

enum class VtermKeyEventType : u8 {
    Press = 1,
    Repeat = 2,
    Release = 3
};

struct VtermTextResult {
    stl::StringView text;
    bool status = false;
};

struct VtermState {
    MouseTrackingState mouse;
    u16 columns = 0;
    u16 rows = 0;
    u8 kittyKeyboardFlags = 0;
    bool metaMode = false;
    bool autoRepeat = false;
    bool synchronizedOutput = false;
    bool animation = false;
};

struct TerminalUpdate {
    const RenderCell* cells = nullptr;
    size_t cellCount = 0;
    CellExtraStore* cellExtras = nullptr;
    u16 columns = 0;
    u16 rows = 0;
    u16 pixelWidth = 0;
    u16 pixelHeight = 0;
    u16 viewOffset = 0;
    u16 historyRows = 0;
    TerminalCursor cursor;
    Rect selection;
    Rect snappedSelection;
    Color selectionForeground;
    Color selectionBackground;
    u8 selectionColorMask = 0;
    bool incremental = false;
    bool screenReverse = false;
    bool blinkVisible = true;
    bool cursorBlink = false;
};

struct VtermOutput {
    stl::StringView pty;
    const TerminalUpdate* terminal = nullptr;
};

struct VtermConsume {
    size_t ptyBytes = 0;
    bool terminal = false;
};

struct TestApi;

struct Vterm {
    virtual void feedPty(stl::StringView bytes) = 0;
    virtual void expose() = 0;
    virtual void resize(u16 width, u16 height) = 0;
    virtual void focus(bool focused) = 0;

    virtual void key(VtKey key, VtModifier modifiers) = 0;
    virtual void character(u8 byte, VtModifier modifiers) = 0;
    virtual void sendBytes(stl::StringView bytes, bool userInput) = 0;
    virtual void kittyKey(VtKey key, u16 modifiers, VtermKeyEventType event) = 0;
    virtual void kittyKey(u32 key, u32 shiftedKey, u32 baseLayoutKey, u16 modifiers, VtermKeyEventType event) = 0;

    virtual bool mouseHighlightRelease(u16 endX, u16 endY, u16 mouseX, u16 mouseY) = 0;
    virtual void locatorPosition(u16 column, u16 row, u16 pixelX, u16 pixelY, u8 buttons) = 0;
    virtual void locatorButton(u8 button, bool pressed) = 0;
    virtual void scrollUp(u16 count) = 0;
    virtual void scrollDown(u16 count) = 0;
    virtual void pageUp() = 0;
    virtual void pageDown() = 0;

    virtual void selectionStart(int pixelX, int pixelY, bool cycleSnapTo) = 0;
    virtual void selectionExtend(int pixelX, int pixelY, bool cycleSnapTo) = 0;
    virtual void selectionUpdate(int pixelX, int pixelY) = 0;
    virtual VtermTextResult selectionFinish() = 0;
    virtual void selectionClear() = 0;
    virtual void selectionRectangular() = 0;
    virtual void paste(stl::StringView text) = 0;
    virtual stl::StringView hyperlinkAt(int pixelX, int pixelY) = 0;

    virtual bool expireSynchronizedOutput(bool force) = 0;
    virtual bool advanceAnimation(bool force) = 0;
    virtual VtermOutput output() = 0;
    virtual void consume(const VtermConsume& consumed) = 0;
    virtual VtermState state() const = 0;
    virtual TestApi* testApi() = 0;

    static Vterm* create(Composer& composer, VtermHost& host, VtermTrace* trace, u16 glyphPx, u16 glyphPy, u16 winPx, u16 winPy);
};
