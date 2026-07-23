/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "vterm.h"

struct VtermTestCell {
    TerminalCell cell;
    const u32* grapheme = nullptr;
    size_t graphemeSize = 0;
    CellColor underlineColor;
};

struct VtermTestState {
    bool screenReverseVideo = false;
    u8 ledState = 0;
    bool reverseWrapMode = false;
    bool nationalReplacementMode = false;
    TerminalCursor::Style cursorStyle = TerminalCursor::Style::hidden;
    TerminalPen pen;
    RectangleOrigin rectangleOrigin{};
    size_t hyperlinkCount = 0;
};

struct TestApi {
    virtual VtermTestState inspect() const = 0;
    virtual bool ansiMode(u32 mode) const = 0;
    virtual bool privateMode(u32 mode) const = 0;
    virtual VtermTestCell cell(u16 row, u16 column) const = 0;
};
