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
#include <std/lib/vector.h>

#include "terminal_types.h"
#include "utf8.h"

#include <cstddef>
#include <deque>
#include <vector>

class CellExtraStore;
struct Composer;

class Frame {
public:
    Frame();

    Frame(Composer& composer_, u16 winPx_, u16 winPy_, u16 nCols_, u16 nRows_, u16& marginTop_, u16& marginBottom_, const TerminalColors* colors_, u16 saveLines_ = 0);

    struct ResizeState {
        Point cursor;
        bool pendingWrap = false;
    };

    ResizeState resize(u16 winPx_, u16 winPy_, u16 nCols_, u16 nRows_, u16& marginTop_, u16& marginBottom_, ResizeState state, bool reflow);

    void dropScrollbackHistory();

    void fillCells(u16 ch, const TerminalCell& attrs);
    void fullCopyCells(RenderCell* const dest) const;
    void deltaCopyCells(RenderCell* const dest) const;

    operator bool() const {
        return cells != nullptr;
    }

    void freeCells() {
        cells = nullptr;
    }

    const TerminalCell& getCell(u16 pY, u16 pX) const;
    TerminalCell& getCell(u16 pY, u16 pX);
    const TerminalCell* getRow(u16 pY) const;
    TerminalCell* writeSpan(u16 pY, u16 startX, u16 count);
    const TerminalCell& getViewCell(u16 pY, u16 pX) const;

    CellExtraStore* cellExtras() const noexcept;
    void collectExtraRefLocations(stl::Vector<u32*>& locations);
    size_t cellCapacity() const noexcept {
        return (size_t)(nCols) * (nRows + saveLines);
    }

    void eraseInRow(u16 pY, u16 startX, u16 count, const TerminalCell& attrs);
    void eraseWideInRow(u16 pY, u16 startX, u16 count, const TerminalCell& attrs);
    TerminalCell* overwriteSpan(u16 pY, u16 startX, u16 count, const TerminalCell& eraseAttrs);
    void clearWideBoundary(u16 pY, u16 boundary, const TerminalCell& attrs);
    void repairWideBoundary(u16 pY, u16 boundary, const TerminalCell& attrs);
    void selectiveEraseInRow(u16 pY, u16 startX, u16 count, const TerminalCell& attrs, u8 protectionMask = 0xff);
    void moveInRow(u16 pY, u16 dstX, u16 srcX, u16 count);
    void copyRow(u16 dstY, u16 srcY, u16 startX, u16 count);
    void rotateRowsUp(u16 top, u16 bottom, u16 count);
    void rotateRowsDown(u16 top, u16 bottom, u16 count);

    void scrollUp(u16 top, u16 bottom, u16 count);
    void scrollDown(u16 top, u16 bottom, u16 count);
    void restoreHistory(u16 count);

    void pageUp(u16 count);
    void pageDown(u16 count);
    bool pageToBottom();

    u16 getHistoryRows() const {
        return history.size();
    };

    u16 getViewOffset() const {
        return viewOffset;
    };

    void expose() {
        damage.expose();
    };

    void resetDamage() {
        damage.reset();
    };

    bool hasDamage() const {
        return damage.start < damage.end;
    }

    TerminalCursor getCursor() const;
    void setCursorPos(u16 pY, u16 pX);
    void setCursorStyle(TerminalCursor::Style cs);
    void setCursorColor(Color color);
    void setSelectionColor(bool foreground, Color color, bool enabled);

    Color getSelectionForeground() const {
        return selectionForeground;
    }

    Color getSelectionBackground() const {
        return selectionBackground;
    }

    u8 getSelectionColorMask() const {
        return selectionColorMask;
    }

    void setBlinkState(bool visible, bool cursor);

    bool getBlinkVisible() const {
        return blinkVisible;
    }

    bool getCursorBlink() const {
        return cursorBlink;
    }

    void setScreenReverseVideo(bool enabled);

    bool getScreenReverseVideo() const {
        return screenReverseVideo;
    }

    enum class SelectSnapTo : u8 {
        Char = 0,
        Word,
        Line,
        COUNT
    };

    void setSelectSnapTo(SelectSnapTo snapTo_) {
        snapTo = snapTo_;
    };

    void cycleSelectSnapTo() {
        snapTo = cycleSelectSnapTo(snapTo);
    };

    Rect& getSelection() {
        return selection;
    };

    const Rect& getSelection() const {
        return selection;
    };

    Rect getSelectionForView() const;
    Rect getSnappedSelection() const;
    bool getSelectedUtf8(std::string& utf8_selection) const;
    Point getLogicalPoint(Point point) const;

    constexpr const static size_t cellSize = sizeof(TerminalCell);

    u64 seqNo = 0;

    u16 winPx = 0;
    u16 winPy = 0;
    u16 nCols = 0;
    u16 nRows = 0;
    u16 saveLines = 0;

private:
    using RowId = u32;

    u16 viewOffset;

    TerminalCell::Ptr cells = nullptr;
    const TerminalColors* colors = nullptr;
    Composer* composer = nullptr;
    std::vector<TerminalCell> erasedRowTemplate;
    TerminalCell erasedRowCell{};
    bool erasedRowTemplateValid = false;
    // Every allocated row belongs to exactly one of these containers.
    std::vector<RowId> screen;
    std::deque<RowId> history;
    std::vector<RowId> freeRows;
    TerminalCursor cursor;
    Rect selection;
    Color selectionForeground = opts.fg;
    Color selectionBackground = opts.bg;
    u8 selectionColorMask = 0;
    bool blinkVisible = true;
    bool cursorBlink = false;
    bool screenReverseVideo = false;
    SelectSnapTo snapTo = SelectSnapTo::Char;

    struct Damage {
        u32 start = 0;
        u32 end = 0;
        u32 totalCells = 0;

        void reset();
        void expose();
        void add(u32 start_, u32 end_);
    };

    Damage damage;

    RowId getLogicalRow(int pY) const;
    const TerminalCell* getLogicalRowPtr(int pY) const;
    const TerminalCell* getViewRowPtr(int pY) const;
    u32 getIdx(u16 pY, u16 pX) const;
    const TerminalCell& operator[](u32 idx) const;
    TerminalCell& operator[](u32 idx);

    void eraseRange(u32 start, u32 end, const TerminalCell& attrs);
    void copyCells(u32 dstIx, u32 srcIx, u32 count);
    void moveCells(u32 dstIx, u32 srcIx, u32 count);

    RenderCell materialize(const TerminalCell& cell, const CellExtraStore& extras) const;
    void damageDeltaCopy(
        RenderCell* dst,
        u32 start,
        u32 count,
        const CellExtraStore& extras
    ) const;

    static SelectSnapTo cycleSelectSnapTo(SelectSnapTo& snapTo) {
        return (SelectSnapTo)(((u8)(snapTo) + 1) % (u8)(SelectSnapTo::COUNT));
    }

    void vscrollSelection(u16 top, u16 bottom, int vertOffset, bool captureHistory);
    void invalidateSelection(const Rect&& damage);
    bool selectionValid() const;

    void highMemUsageReport();
};
