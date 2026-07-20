/* This file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the file LICENSE for the full license.
 */

#pragma once

#include "terminal_types.h"
#include "utf8.h"

#include <deque>
#include <map>
#include <set>
#include <vector>

class Frame {
public:
    using Grapheme = std::vector<uint32_t>;
    Frame();

    Frame(uint16_t winPx_, uint16_t winPy_,
          uint16_t nCols_, uint16_t nRows_,
          uint16_t& marginTop_, uint16_t& marginBottom_,
          uint16_t saveLines_ = 0);

    void resize(uint16_t winPx_, uint16_t winPy_,
                uint16_t nCols_, uint16_t nRows_,
                uint16_t& marginTop_, uint16_t& marginBottom_);

    void dropScrollbackHistory();

    void fillCells(uint16_t ch, const TerminalCell& attrs);
    void fullCopyCells(TerminalCell* const dest) const;
    void deltaCopyCells(TerminalCell* const dest) const;

    operator bool() const {
        return cells != nullptr;
    }
    void freeCells() {
        cells = nullptr;
    }

    const TerminalCell& getCell(uint16_t pY, uint16_t pX) const;
    TerminalCell& getCell(uint16_t pY, uint16_t pX);
    const TerminalCell& getViewCell(uint16_t pY, uint16_t pX) const;

    uint32_t internGrapheme(const Grapheme& codepoints);
    const Grapheme& getGrapheme(uint32_t id) const;

    void eraseInRow(uint16_t pY, uint16_t startX, uint16_t count,
                    const TerminalCell& attrs);
    void selectiveEraseInRow(uint16_t pY, uint16_t startX, uint16_t count,
                             const TerminalCell& attrs);
    void moveInRow(uint16_t pY, uint16_t dstX, uint16_t srcX,
                   uint16_t count);
    void copyRow(uint16_t dstY, uint16_t srcY, uint16_t startX,
                 uint16_t count);

    void scrollUp(uint16_t top, uint16_t bottom, uint16_t count);
    void scrollDown(uint16_t top, uint16_t bottom, uint16_t count);
    void restoreHistory(uint16_t count);

    void pageUp(uint16_t count);
    void pageDown(uint16_t count);
    bool pageToBottom();

    uint16_t getHistoryRows() const {
        return history.size();
    };
    uint16_t getViewOffset() const {
        return viewOffset;
    };
    void collectHyperlinkIds(std::set<uint32_t>& ids) const;
    void recolorPalette(uint16_t index, Color color);
    void recolorDefault(bool foreground, Color color);

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
    void setCursorPos(uint16_t pY, uint16_t pX);
    void setCursorStyle(TerminalCursor::Style cs);
    void setCursorColor(Color color);
    void setSelectionColor(bool foreground, Color color, bool enabled);
    Color getSelectionForeground() const { return selectionForeground; }
    Color getSelectionBackground() const { return selectionBackground; }
    uint8_t getSelectionColorMask() const { return selectionColorMask; }
    void setBlinkState(bool visible, bool cursor) {
        blinkVisible = visible;
        cursorBlink = cursor;
    }
    bool getBlinkVisible() const { return blinkVisible; }
    bool getCursorBlink() const { return cursorBlink; }
    void setScreenReverseVideo(bool enabled) {
        screenReverseVideo = enabled;
        expose();
    }
    bool getScreenReverseVideo() const { return screenReverseVideo; }

    enum class SelectSnapTo : uint8_t {
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

    uint64_t seqNo = 0;

    uint16_t winPx = 0;
    uint16_t winPy = 0;
    uint16_t nCols = 0;
    uint16_t nRows = 0;
    uint16_t saveLines = 0;

private:
    struct GraphemeStore {
        std::vector<Grapheme> values = {Grapheme{}};
        std::map<Grapheme, uint32_t> ids;
    };

    using RowId = uint32_t;

    uint16_t viewOffset;

    TerminalCell::Ptr cells = nullptr;
    std::shared_ptr<GraphemeStore> graphemes =
        std::make_shared<GraphemeStore>();
    // Every allocated row belongs to exactly one of these containers.
    std::vector<RowId> screen;
    std::deque<RowId> history;
    std::vector<RowId> freeRows;
    TerminalCursor cursor;
    Rect selection;
    Color selectionForeground = opts.fg;
    Color selectionBackground = opts.bg;
    uint8_t selectionColorMask = 0;
    bool blinkVisible = true;
    bool cursorBlink = false;
    bool screenReverseVideo = false;
    SelectSnapTo snapTo = SelectSnapTo::Char;

    struct Damage {
        uint32_t start = 0;
        uint32_t end = 0;
        uint32_t totalCells = 0;

        void reset();
        void expose();
        void add(uint32_t start_, uint32_t end_);
    };
    Damage damage;

    RowId getLogicalRow(int pY) const;
    const TerminalCell* getLogicalRowPtr(int pY) const;
    const TerminalCell* getViewRowPtr(int pY) const;
    uint32_t getIdx(uint16_t pY, uint16_t pX) const;
    const TerminalCell& operator[](uint32_t idx) const;
    TerminalCell& operator[](uint32_t idx);

    void eraseRange(uint32_t start, uint32_t end,
                    const TerminalCell& attrs);
    void copyCells(uint32_t dstIx, uint32_t srcIx, uint32_t count);
    void moveCells(uint32_t dstIx, uint32_t srcIx, uint32_t count);

    void damageDeltaCopy(
        TerminalCell* dst, uint32_t start, uint32_t count) const;

    static SelectSnapTo cycleSelectSnapTo(SelectSnapTo& snapTo) {
        return static_cast<SelectSnapTo>(
            (static_cast<uint8_t>(snapTo) + 1) %
            static_cast<uint8_t>(SelectSnapTo::COUNT));
    }

    void vscrollSelection(uint16_t top, uint16_t bottom,
                          int vertOffset, bool captureHistory);
    void invalidateSelection(const Rect&& damage);

    void highMemUsageReport();
};

#include "frame.icc"
