/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "rect.h"
#include "terminal_types.h"

#include <plt/input.h>

#include <std/str/view.h>
#include <std/sys/types.h>

#include <cstddef>
#include <cstdint>

struct Composer;
struct CellExtraStore;
struct VtermHost;
struct VtermTrace;

enum class VtModifier : u8 {
    none = 0,
    shift = 1,
    control = 2,
    shift_control = 3,
    alt = 4,
    shift_alt = 5,
    control_alt = 6,
    shift_control_alt = 7,
    super = 8
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
};

struct TerminalUpdate {
    const TerminalCellSpan* spans = nullptr;
    size_t spanCount = 0;
    const TerminalColors* colors = nullptr;
    u32 viewOffset = 0;
    u32 historyRows = 0;
    TerminalCursor cursor;
    Rect selection;
    Rect snappedSelection;
    Color selectionForeground;
    Color selectionBackground;
    u8 selectionColorMask = 0;
    u32 hoveredHyperlink = 0;
    u32 hoveredLinkBegin = 0;
    u32 hoveredLinkEnd = 0;
    bool screenReverse = false;
    bool blinkVisible = true;
    bool cursorBlink = false;
};

struct TestApi;

struct Vterm {
    virtual void feedPty(stl::StringView bytes) = 0;
    virtual void expose() = 0;
    virtual void sendBytes(stl::StringView bytes, bool userInput) = 0;

    virtual bool expireSynchronizedOutput(bool force) = 0;
    virtual bool advanceAnimation(bool force) = 0;
    virtual const TerminalUpdate* output() = 0;
    virtual void consume() = 0;
    virtual VtermState state() const = 0;
    virtual TestApi* testApi() = 0;

    static Vterm* create(Composer& composer, VtermHost& host, VtermTrace* trace);
};
