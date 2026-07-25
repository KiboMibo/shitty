/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "rect.h"
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
    bool synchronizedOutput = false;
    bool animation = false;
};

struct TerminalUpdate {
    const RenderCell* cells = nullptr;
    size_t cellCount = 0;
    u16 viewOffset = 0;
    u16 historyRows = 0;
    TerminalCursor cursor;
    Rect selection;
    Rect snappedSelection;
    Color selectionForeground;
    Color selectionBackground;
    u8 selectionColorMask = 0;
    u32 hoveredHyperlink = 0;
    u32 hoveredLinkBegin = 0;
    u32 hoveredLinkEnd = 0;
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
    virtual void sendBytes(stl::StringView bytes, bool userInput) = 0;

    virtual bool expireSynchronizedOutput(bool force) = 0;
    virtual bool advanceAnimation(bool force) = 0;
    virtual VtermOutput output() = 0;
    virtual void consume(const VtermConsume& consumed) = 0;
    virtual VtermState state() const = 0;
    virtual TestApi* testApi() = 0;

    static Vterm* create(Composer& composer, VtermHost& host, VtermTrace* trace);
};
