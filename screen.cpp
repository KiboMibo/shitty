/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

/* part of this file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the file LICENSE.GPL3 for the full license.
 */

#include "screen.h"

#include "cell_extra_store.h"
#include "composer.h"
#include "utf8.h"

#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>

#include <utf8proc.h>

#include <algorithm>
#include <cassert>
#include <deque>
#include <utility>
#include <vector>

namespace stl {}

using namespace stl;

// The complete geometry-independent state of one screen: content in its
// source geometry (storage adopted by move), view/selection anchors, and the
// presentation scalars the terminal does not re-push after a rebuild.
struct ResizeState {
    u16 columns = 0;
    u16 rows = 0;
    u16 saveLines = 0;
    bool active = false;
    TerminalCell::Ptr cells;
    std::vector<u32> screen;
    std::deque<u32> history;
    u16 viewOffset = 0;
    Rect selection;
    Screen::SelectSnapTo snapTo = Screen::SelectSnapTo::Char;
    Color selectionForeground{};
    Color selectionBackground{};
    u8 selectionColorMask = 0;
    bool blinkVisible = true;
    bool cursorBlink = false;
    bool screenReverseVideo = false;
    TerminalCursor cursor;
};

namespace {

    struct ScreenImpl final: public Screen {
        ScreenImpl();
        ScreenImpl(Composer& composer, u16 columns, u16 rows, const TerminalColors* colors, u16 saveLines);

        ResizeState* moveInto() override;
        void dropScrollbackHistory() override;

        void fillCells(u16 ch, const TerminalCell& attrs) override;
        void setLineAttribute(u16 row, u8 attribute) override;
        u8 lineAttribute(u16 row) const noexcept override;
        bool hasProtection(u16 row, u8 mask) const noexcept override;
        bool wrapped(u16 row, u16 column) const noexcept override;
        void setWrapped(u16 row, u16 column) override;
        void moveWrap(u16 row, u16 sourceColumn, u16 destinationColumn) override;
        void writeGrapheme(u16 row, u16 column, const u32* codepoints, size_t count, bool wide, const TerminalCell& attrs, u32 hyperlink, u32 semantic, u8 lineAttribute, const TerminalCell& eraseAttrs) override;
        void writeAsciiRun(u16 row, u16 column, const u8* input, u16 count, const TerminalCell& attrs, u32 hyperlink, u32 semantic, u8 lineAttribute, const TerminalCell& eraseAttrs) override;
        void fillRectangle(u16 top, u16 left, u16 bottom, u16 right, u32 codepoint, const TerminalCell& attrs, const TerminalCell& eraseAttrs) override;
        void copyRectangle(u16 sourceTop, u16 sourceLeft, u16 targetTop, u16 targetLeft, u16 height, u16 width, const TerminalCell& eraseAttrs) override;
        void changeRectangleAttributes(u16 top, u16 left, u16 bottom, u16 right, const u32* modes, size_t modeCount, bool reverse) override;
        u16 checksum(u16 top, u16 left, u16 bottom, u16 right) const noexcept override;
        void appendPrintableLine(u16 row, std::string& output) const override;
        stl::StringView hyperlinkAt(u16 row, u16 column) const noexcept override;
        TerminalCell testCell(u16 row, u16 column) const noexcept override;
        void fullCopyCells(RenderCell* dest) const override;
        void deltaCopyCells(RenderCell* dest) override;

        bool active() const noexcept override;

        CellExtraStore* cellExtras() const noexcept override;
        void collectExtraRefLocations(stl::Vector<u32*>& locations) override;
        size_t cellCapacity() const noexcept override;

        void eraseInRow(u16 row, u16 start, u16 count, const TerminalCell& attrs) override;
        void eraseWideInRow(u16 row, u16 start, u16 count, const TerminalCell& attrs) override;
        void clearWideBoundary(u16 row, u16 boundary, const TerminalCell& attrs) override;
        void repairWideBoundary(u16 row, u16 boundary, const TerminalCell& attrs) override;
        void selectiveEraseInRow(u16 row, u16 start, u16 count, const TerminalCell& attrs, u8 protectionMask) override;
        void moveInRow(u16 row, u16 destination, u16 source, u16 count) override;
        void copyRow(u16 destinationRow, u16 sourceRow, u16 start, u16 count) override;
        void rotateRowsUp(u16 top, u16 bottom, u16 count) override;
        void rotateRowsDown(u16 top, u16 bottom, u16 count) override;

        void scrollUp(u16 top, u16 bottom, u16 count) override;
        void scrollDown(u16 top, u16 bottom, u16 count) override;
        void restoreHistory(u16 count) override;

        void pageUp(u16 count) override;
        void pageDown(u16 count) override;
        bool pageToBottom() override;

        u16 getHistoryRows() const noexcept override;
        u16 getViewOffset() const noexcept override;
        u16 columns() const noexcept override;
        u16 rows() const noexcept override;

        void expose() override;
        void resetDamage() override;
        bool hasDamage() const noexcept override;

        TerminalCursor getCursor() const override;
        void setCursorPos(u16 row, u16 column) override;
        void setCursorStyle(TerminalCursor::Style style) override;
        void setCursorColor(Color color) override;
        void setSelectionColor(bool foreground, Color color, bool enabled) override;

        Color getSelectionForeground() const noexcept override;
        Color getSelectionBackground() const noexcept override;
        u8 getSelectionColorMask() const noexcept override;

        void setBlinkState(bool visible, bool cursor) override;
        bool getBlinkVisible() const noexcept override;
        bool getCursorBlink() const noexcept override;

        void setScreenReverseVideo(bool enabled) override;
        bool getScreenReverseVideo() const noexcept override;

        void setSelectSnapTo(SelectSnapTo snapTo) override;
        void cycleSelectSnapTo() override;
        Rect& getSelection() override;
        const Rect& getSelection() const override;
        Rect getSelectionForView() const override;
        Rect getSnappedSelection() const override;
        bool getSelectedUtf8(std::string& text) const override;
        Point getLogicalPoint(Point point) const override;

        using RowId = u32;

        u16 nCols = 0;
        u16 nRows = 0;
        u16 saveLines = 0;
        u16 viewOffset = 0;

        TerminalCell::Ptr cells = nullptr;
        const TerminalColors* colors = nullptr;
        Composer* composer = nullptr;
        stl::ObjPool* pool = nullptr;
        std::vector<TerminalCell> erasedRowTemplate;
        TerminalCell erasedRowCell{};
        bool erasedRowTemplateValid = false;
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
        stl::Buffer damageStorage;

        struct Damage {
            u32* cells = nullptr;
            u32* epochs = nullptr;
            size_t capacity = 0;
            size_t count = 0;
            u32 epoch = 0;
            u16 width = 0;
            u16 height = 0;

            Damage& operator=(Damage&& other) noexcept;
            void configure(void* storage, u16 columns, u16 rows);
            void reset();
            void expose();
            bool hasDamage() const noexcept;
            void addCell(u16 row, u16 column);
            void addRow(u16 row, u16 begin, u16 end);
            void addRect(u16 top, u16 left, u16 bottom, u16 right);
        };

        Damage damage;

        RowId getLogicalRow(int row) const;
        const TerminalCell* getLogicalRowPtr(int row) const;
        const TerminalCell* getViewRowPtr(int row) const;
        u32 getIdx(u16 row, u16 column) const;
        const TerminalCell& operator[](u32 index) const;
        TerminalCell& operator[](u32 index);

        void eraseRange(u32 start, u32 end, const TerminalCell& attrs);
        void copyCells(u32 destination, u32 source, u32 count);
        void moveCells(u32 destination, u32 source, u32 count);
        void damageCell(u16 row, u16 column);
        void damageRow(u16 row, u16 begin, u16 end);
        void damageRectangle(u16 top, u16 left, u16 bottom, u16 right);
        void resizeDamage(u16 columns, u16 rows);
        TerminalCell* dirtySpan(u16 row, u16 start, u16 count);
        TerminalCell* overwriteWideSpan(u16 row, u16 start, u16 count, const TerminalCell& eraseAttrs);
        TerminalCell* prepareSpan(u16 row, u16 start, u16 count, const TerminalCell& eraseAttrs);
        TerminalCell& prepareCell(u16 row, u16 column, const TerminalCell& eraseAttrs);

        void layout(ResizeState& state, u16 columns, u16 rows, const TerminalColors* colors, bool reflow, Cursor* cursorState);
        void layoutCopy(ResizeState& state, u16 columns, u16 rows, Cursor* cursorState);
        void layoutReflow(ResizeState& state, u16 columns, u16 rows, Cursor* cursorState);

        RenderCell materialize(const TerminalCell& cell, const CellExtraStore& extras) const;
        void damageDeltaCopy(RenderCell* destination, const TerminalCell* source, u16 count, const CellExtraStore& extras) const;

        static SelectSnapTo nextSelectSnapTo(SelectSnapTo snapTo);

        void vscrollSelection(u16 top, u16 bottom, int verticalOffset, bool captureHistory);
        void invalidateSelection(const Rect&& changed);
        bool selectionValid() const;

        static constexpr size_t cellSize = sizeof(TerminalCell);
    };

    u32 wordClass(u32 codepoint) {
        constexpr u32 whitespaceClass = 0x110000;
        constexpr u32 identifierClass = 0x110001;
        switch (utf8proc_category(codepoint)) {
            case UTF8PROC_CATEGORY_LU:
            case UTF8PROC_CATEGORY_LL:
            case UTF8PROC_CATEGORY_LT:
            case UTF8PROC_CATEGORY_LM:
            case UTF8PROC_CATEGORY_LO:
            case UTF8PROC_CATEGORY_MN:
            case UTF8PROC_CATEGORY_MC:
            case UTF8PROC_CATEGORY_ME:
            case UTF8PROC_CATEGORY_ND:
            case UTF8PROC_CATEGORY_NL:
            case UTF8PROC_CATEGORY_NO:
            case UTF8PROC_CATEGORY_PC:
                return identifierClass;
            case UTF8PROC_CATEGORY_ZS:
            case UTF8PROC_CATEGORY_ZL:
            case UTF8PROC_CATEGORY_ZP:
                return whitespaceClass;
            default:
                // Adjacent repetitions of one punctuation/symbol codepoint form
                // a useful selectable run, while unlike punctuation stays split.
                return codepoint;
        }
    }

    // A column shrink can copy the leading half of a wide glyph while
    // clipping its continuation.  Never publish or retain such a partial
    // cell: editing code relies on the same lead/continuation invariant.
    void normalizeWideRow(TerminalCell* row, u16 columns) {
        for (u16 column = 0; column < columns; ++column) {
            const bool orphanLead = row[column].dwidth && (column + 1 == columns || !row[column + 1].dwidth_cont);
            const bool orphanContinuation = row[column].dwidth_cont && (column == 0 || !row[column - 1].dwidth);
            if (orphanLead || orphanContinuation) {
                row[column] = TerminalCell{};
            }
        }
    }

    const TerminalCell* stateRowPtr(const ResizeState& state, int row) {
        const u32 id = row < 0 ? state.history[state.history.size() + row] : state.screen[row];
        return state.cells.get() + (size_t)(id)*state.columns;
    }

}

ScreenImpl::ScreenImpl() {
}

Screen* Screen::create(Composer& composer, ObjPool& pool) {
    ScreenImpl* const result = pool.make<ScreenImpl>();
    result->composer = &composer;
    result->pool = &pool;
    return result;
}

Screen* Screen::create(Composer& composer, ObjPool& pool, u16 columns, u16 rows, const TerminalColors* colors, u16 saveLines) {
    ScreenImpl* const result = pool.make<ScreenImpl>(composer, columns, rows, colors, saveLines);
    result->pool = &pool;
    return result;
}

Screen* Screen::create(Composer& composer, ObjPool& pool, ResizeState& state, u16 columns, u16 rows, const TerminalColors* colors, bool reflow, Cursor* cursor) {
    ScreenImpl* const result = pool.make<ScreenImpl>();
    result->composer = &composer;
    result->pool = &pool;
    if (state.active) {
        result->layout(state, columns, rows, colors, reflow, cursor);
    }
    return result;
}

bool ScreenImpl::active() const noexcept {
    return cells != nullptr;
}

size_t ScreenImpl::cellCapacity() const noexcept {
    return (size_t)(nCols) * (nRows + saveLines);
}

u16 ScreenImpl::getHistoryRows() const noexcept {
    return history.size();
}

u16 ScreenImpl::getViewOffset() const noexcept {
    return viewOffset;
}

u16 ScreenImpl::columns() const noexcept {
    return nCols;
}

u16 ScreenImpl::rows() const noexcept {
    return nRows;
}

void ScreenImpl::expose() {
    damage.expose();
}

void ScreenImpl::resetDamage() {
    damage.reset();
}

bool ScreenImpl::hasDamage() const noexcept {
    return damage.hasDamage();
}

Color ScreenImpl::getSelectionForeground() const noexcept {
    return selectionForeground;
}

Color ScreenImpl::getSelectionBackground() const noexcept {
    return selectionBackground;
}

u8 ScreenImpl::getSelectionColorMask() const noexcept {
    return selectionColorMask;
}

bool ScreenImpl::getBlinkVisible() const noexcept {
    return blinkVisible;
}

bool ScreenImpl::getCursorBlink() const noexcept {
    return cursorBlink;
}

bool ScreenImpl::getScreenReverseVideo() const noexcept {
    return screenReverseVideo;
}

void ScreenImpl::setSelectSnapTo(SelectSnapTo value) {
    snapTo = value;
}

Screen::SelectSnapTo ScreenImpl::nextSelectSnapTo(SelectSnapTo value) {
    return (SelectSnapTo)(((u8)(value) + 1) % (u8)(SelectSnapTo::COUNT));
}

void ScreenImpl::cycleSelectSnapTo() {
    snapTo = nextSelectSnapTo(snapTo);
}

Rect& ScreenImpl::getSelection() {
    return selection;
}

const Rect& ScreenImpl::getSelection() const {
    return selection;
}

void ScreenImpl::setBlinkState(bool visible, bool cursor) {
    blinkVisible = visible;
    cursorBlink = cursor;
}

void ScreenImpl::setScreenReverseVideo(bool enabled) {
    screenReverseVideo = enabled;
    expose();
}

void ScreenImpl::setSelectionColor(bool foreground, Color color, bool enabled) {
    const u8 bit = foreground ? 1 : 2;
    if (foreground) {
        selectionForeground = color;
    } else {
        selectionBackground = color;
    }
    if (enabled) {
        selectionColorMask |= bit;
    } else {
        selectionColorMask &= ~bit;
    }
    expose();
}

CellExtraStore* ScreenImpl::cellExtras() const noexcept {
    return composer == nullptr ? nullptr : composer->cellExtras;
}

ScreenImpl::ScreenImpl(Composer& composer_, u16 nCols_, u16 nRows_, const TerminalColors* colors_, u16 saveLines_)
    : nCols(nCols_)
    , nRows(nRows_)
    , saveLines(saveLines_)
    , viewOffset(0)
    , cells(TerminalCell::make(nCols, nRows + saveLines))
    , colors(colors_)
    , composer(&composer_)
    , screen(nRows)
{
    for (RowId row = 0; row < nRows; ++row) {
        screen[row] = row;
    }
    for (RowId row = nRows; row < nRows + saveLines; ++row) {
        freeRows.push_back(row);
    }
    resizeDamage(nCols, nRows);
}

void ScreenImpl::dropScrollbackHistory() {
    viewOffset = 0;
    if (!selection.null() && selection.tl.y < 0) {
        selection.clear();
    }
    while (!history.empty()) {
        freeRows.push_back(history.front());
        history.pop_front();
    }
    expose();
}

void ScreenImpl::collectExtraRefLocations(stl::Vector<u32*>& locations) {
    const auto collectRow = [&](RowId row) {
        TerminalCell* first = cells.get() + row * nCols;
        for (u16 column = 0; column < nCols; ++column) {
            if (first[column].hasExtra()) {
                locations.pushBack(&first[column].payload);
            }
        }
    };
    for (RowId row : screen) {
        collectRow(row);
    }
    for (RowId row : history) {
        collectRow(row);
    }
}

ResizeState* ScreenImpl::moveInto() {
    ResizeState* const state = pool->make<ResizeState>();
    state->columns = nCols;
    state->rows = nRows;
    state->saveLines = saveLines;
    state->active = active();
    state->cells = std::move(cells);
    state->screen = std::move(screen);
    state->history = std::move(history);
    state->viewOffset = viewOffset;
    state->selection = selection;
    state->snapTo = snapTo;
    state->selectionForeground = selectionForeground;
    state->selectionBackground = selectionBackground;
    state->selectionColorMask = selectionColorMask;
    state->blinkVisible = blinkVisible;
    state->cursorBlink = cursorBlink;
    state->screenReverseVideo = screenReverseVideo;
    state->cursor = cursor;
    cells = nullptr;
    return state;
}

void ScreenImpl::layout(ResizeState& state, u16 nCols_, u16 nRows_, const TerminalColors* colors_, bool reflow, Cursor* cursorState) {
    colors = colors_;
    saveLines = state.saveLines;
    viewOffset = state.viewOffset;
    selection = state.selection;
    snapTo = state.snapTo;
    selectionForeground = state.selectionForeground;
    selectionBackground = state.selectionBackground;
    selectionColorMask = state.selectionColorMask;
    blinkVisible = state.blinkVisible;
    cursorBlink = state.cursorBlink;
    screenReverseVideo = state.screenReverseVideo;
    cursor = state.cursor;

    if (reflow && state.columns != nCols_) {
        layoutReflow(state, nCols_, nRows_, cursorState);
    } else {
        layoutCopy(state, nCols_, nRows_, cursorState);
    }
    resizeDamage(nCols, nRows);
    expose();
}

void ScreenImpl::layoutCopy(ResizeState& state, u16 nCols_, u16 nRows_, Cursor* cursorState) {
    std::deque<RowId> sourceHistory = std::move(state.history);
    std::vector<RowId> sourceScreen = std::move(state.screen);
    size_t visibleStart = 0;

    // An interactive shrink preserves every row above the cursor that still
    // fits by pushing the top rows into history.  Scrolling by the full
    // height delta would needlessly discard additional rows whenever the
    // cursor is not on the old bottom row.
    if (cursorState != nullptr && cursorState->position.y + 1 > (int)(nRows_)) {
        const u16 preScroll = (u16)(cursorState->position.y + 1 - nRows_);
        if (saveLines != 0) {
            for (u16 k = 0; k < preScroll; ++k) {
                sourceHistory.push_back(sourceScreen[visibleStart + k]);
                if (sourceHistory.size() > saveLines) {
                    sourceHistory.pop_front();
                }
            }
            if (!selection.null()) {
                if (selection.br.y >= (int)(state.rows)) {
                    if (selection.tl.y < (int)(state.rows)) {
                        selection.clear();
                    }
                } else {
                    selection.tl.y -= preScroll;
                    selection.br.y -= preScroll;
                    if (selection.tl.y < -(int)(sourceHistory.size())) {
                        selection.clear();
                    }
                }
            }
            if (viewOffset != 0) {
                viewOffset = (u16)(std::min<size_t>(viewOffset + preScroll, sourceHistory.size()));
            }
        } else if (!selection.null()) {
            const bool topInside = selection.tl.y >= 0 && selection.tl.y < (int)(state.rows);
            const bool bottomInside = selection.br.y >= 0 && selection.br.y < (int)(state.rows);
            if (topInside != bottomInside) {
                selection.clear();
            } else if (topInside) {
                selection.tl.y -= preScroll;
                selection.br.y -= preScroll;
                if (selection.tl.y < 0) {
                    selection.clear();
                }
            } else if (!(selection.br.y < 0 || selection.tl.y >= (int)(state.rows))) {
                selection.clear();
            }
        }
        visibleStart = preScroll;
        cursorState->position.y -= preScroll;
    }

    viewOffset = (u16)(std::min<size_t>(viewOffset, sourceHistory.size()));

    // An interactive growth restores rows from history above the screen.
    std::vector<RowId> restored;
    if (cursorState != nullptr && nRows_ > state.rows) {
        const u16 restore = (u16)(std::min<size_t>(nRows_ - state.rows, sourceHistory.size()));
        for (u16 k = 0; k < restore; ++k) {
            restored.push_back(sourceHistory.back());
            sourceHistory.pop_back();
        }
        std::reverse(restored.begin(), restored.end());
        cursorState->position.y += restore;
        if (!selection.null()) {
            selection.tl.y += restore;
            selection.br.y += restore;
        }
        viewOffset = viewOffset > restore ? viewOffset - restore : 0;
    }

    nCols = nCols_;
    nRows = nRows_;
    cells = TerminalCell::make(nCols_, nRows_ + saveLines);
    const u16 rowLen = std::min(state.columns, nCols_);
    const auto emit = [&](RowId source, u16 target) {
        TerminalCell* const row = cells.get() + (size_t)(target)*nCols_;
        memcpy(row, state.cells.get() + (size_t)(source)*state.columns, rowLen * cellSize);
        normalizeWideRow(row, nCols_);
    };

    u16 outRow = 0;
    for (const RowId row : restored) {
        emit(row, outRow++);
    }
    for (size_t k = visibleStart; k < sourceScreen.size() && outRow < nRows_; ++k) {
        emit(sourceScreen[k], outRow++);
    }
    const u16 historyCount = (u16)(sourceHistory.size());
    for (u16 k = 0; k < historyCount; ++k) {
        emit(sourceHistory[k], nRows_ + k);
    }

    screen.resize(nRows_);
    for (RowId row = 0; row < nRows_; ++row) {
        screen[row] = row;
    }
    history.clear();
    for (RowId row = nRows_; row < nRows_ + historyCount; ++row) {
        history.push_back(row);
    }
    freeRows.clear();
    for (RowId row = nRows_ + historyCount; row < nRows_ + saveLines; ++row) {
        freeRows.push_back(row);
    }
    if (!selectionValid()) {
        selection.clear();
    }
}

void ScreenImpl::layoutReflow(ResizeState& state, u16 nCols_, u16 nRows_, Cursor* cursorState) {
    struct LogicalLine {
        std::vector<TerminalCell> cells;
        bool reflowable = true;
    };

    struct Anchor {
        int oldRow = 0;
        int oldColumn = 0;
        size_t line = 0;
        size_t offset = 0;
        Point mapped;
        bool found = false;
    };

    struct Boundary {
        size_t row = 0;
        int column = 0;
    };

    const int oldHistoryCount = state.history.size();
    const bool wasScrolled = viewOffset != 0;
    const int oldTotalRows = oldHistoryCount + state.rows;
    // Without a cursor the bottom of the content is the stable point.
    Anchor cursorAnchor{oldTotalRows - 1, 0};
    if (cursorState != nullptr) {
        cursorAnchor = Anchor{
            oldHistoryCount + cursorState->position.y,
            cursorState->position.x + (cursorState->pendingWrap ? 1 : 0),
        };
    }
    Anchor viewAnchor{oldHistoryCount - viewOffset, 0};
    Anchor screenAnchor{oldHistoryCount, 0};
    Anchor selectionStart;
    Anchor selectionEnd;
    const bool keepSelection = !selection.null() && !selection.rectangular;
    if (keepSelection) {
        selectionStart.oldRow = oldHistoryCount + selection.tl.y;
        selectionStart.oldColumn = selection.tl.x;
        selectionEnd.oldRow = oldHistoryCount + selection.br.y;
        selectionEnd.oldColumn = selection.br.x;
    }
    std::vector<Anchor*> anchors = {&cursorAnchor, &viewAnchor, &screenAnchor};
    if (keepSelection) {
        anchors.push_back(&selectionStart);
        anchors.push_back(&selectionEnd);
    }

    const auto cellHasContent = [](const TerminalCell& source) {
        TerminalCell cell = source;
        cell.wrap = 0;
        cell.line_attr = 0;
        return cell != TerminalCell{};
    };

    std::vector<LogicalLine> lines;
    bool continueLine = false;
    for (int oldRow = 0; oldRow < oldTotalRows; ++oldRow) {
        const TerminalCell* row = stateRowPtr(state, oldRow - oldHistoryCount);
        const bool normalWidth = row[0].line_attr == 0;
        const bool join = continueLine && normalWidth;
        if (!join) {
            lines.emplace_back();
        }
        LogicalLine& line = lines.back();
        line.reflowable &= normalWidth;
        const size_t rowOffset = line.cells.size();

        int contentEnd = 0;
        int wrapEnd = 0;
        for (int column = 0; column < state.columns; ++column) {
            if (cellHasContent(row[column])) {
                contentEnd = column + 1;
            }
            if (row[column].wrap) {
                wrapEnd = column + 1;
            }
        }
        int copyEnd = wrapEnd ? wrapEnd : contentEnd;
        if (!normalWidth) {
            copyEnd = state.columns;
        }
        for (Anchor* anchor : anchors) {
            if (anchor->oldRow == oldRow) {
                anchor->line = lines.size() - 1;
                anchor->offset = rowOffset + std::min(anchor->oldColumn, (int)(state.columns));
                anchor->found = true;
                copyEnd = std::max(copyEnd, std::min(anchor->oldColumn, (int)(state.columns)));
            }
        }
        for (int column = 0; column < copyEnd; ++column) {
            TerminalCell cell = row[column];
            cell.wrap = 0;
            line.cells.push_back(cell);
        }
        continueLine = wrapEnd && normalWidth;
    }

    std::vector<std::vector<TerminalCell>> output;
    for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        LogicalLine& line = lines[lineIndex];
        std::vector<Boundary> boundaries(line.cells.size() + 1);
        const size_t lineOutputStart = output.size();
        size_t outputRow = output.size();
        output.emplace_back(nCols_);
        int column = 0;
        boundaries[0] = {outputRow, 0};

        if (line.reflowable) {
            size_t offset = 0;
            while (offset < line.cells.size()) {
                const bool wide = line.cells[offset].dwidth && offset + 1 < line.cells.size() && line.cells[offset + 1].dwidth_cont;
                const size_t width = wide ? 2 : 1;
                if (width > nCols_) {
                    boundaries[offset] = {outputRow, column};
                    ++offset;
                    boundaries[offset] = {outputRow, column};
                    if (wide) {
                        ++offset;
                        boundaries[offset] = {outputRow, column};
                    }
                    continue;
                }
                if (column + (int)(width) > nCols_) {
                    output[outputRow][column ? column - 1 : nCols_ - 1].wrap = 1;
                    outputRow = output.size();
                    output.emplace_back(nCols_);
                    column = 0;
                }
                boundaries[offset] = {outputRow, column};
                for (size_t cellIndex = 0; cellIndex < width; ++cellIndex) {
                    TerminalCell cell = line.cells[offset + cellIndex];
                    cell.wrap = 0;
                    output[outputRow][column++] = cell;
                    boundaries[offset + cellIndex + 1] = {outputRow, column};
                }
                offset += width;
                if (column == nCols_ && offset < line.cells.size()) {
                    output[outputRow][nCols_ - 1].wrap = 1;
                    outputRow = output.size();
                    output.emplace_back(nCols_);
                    column = 0;
                    boundaries[offset] = {outputRow, 0};
                }
            }
        } else {
            const size_t count = std::min<size_t>(line.cells.size(), nCols_);
            for (size_t offset = 0; offset < count; ++offset) {
                output[outputRow][offset] = line.cells[offset];
                output[outputRow][offset].wrap = 0;
                boundaries[offset] = {outputRow, (int)(offset)};
                boundaries[offset + 1] = {outputRow, (int)(offset + 1)};
            }
            for (size_t offset = count + 1; offset < boundaries.size(); ++offset) {
                boundaries[offset] = {outputRow, (int)(count)};
            }
        }

        for (size_t row = lineOutputStart; row < output.size(); ++row) {
            normalizeWideRow(output[row].data(), nCols_);
        }

        for (Anchor* anchor : anchors) {
            if (!anchor->found || anchor->line != lineIndex) {
                continue;
            }
            const Boundary boundary = boundaries[std::min(anchor->offset, boundaries.size() - 1)];
            anchor->mapped = Point(boundary.column, (int)(boundary.row));
        }
    }

    const size_t cursorScreenStart = cursorAnchor.mapped.y >= nRows_ ? cursorAnchor.mapped.y - (nRows_ - 1) : 0;
    size_t preferredScreenStart = screenAnchor.mapped.y;
    if (nRows_ > state.rows) {
        preferredScreenStart -= std::min<size_t>(preferredScreenStart, nRows_ - state.rows);
    }
    const size_t screenStart = std::max(preferredScreenStart, cursorScreenStart);
    while (output.size() < screenStart + nRows_) {
        output.emplace_back(nCols_);
    }
    const size_t retainedStart = screenStart > saveLines ? screenStart - saveLines : 0;
    const size_t historyCount = screenStart - retainedStart;

    cells = TerminalCell::make(nCols_, nRows_ + saveLines);
    for (size_t row = 0; row < nRows_; ++row) {
        memcpy(cells.get() + row * nCols_, output[screenStart + row].data(), nCols_ * cellSize);
    }
    for (size_t row = 0; row < historyCount; ++row) {
        memcpy(cells.get() + (nRows_ + row) * nCols_, output[retainedStart + row].data(), nCols_ * cellSize);
    }

    nCols = nCols_;
    nRows = nRows_;
    screen.resize(nRows_);
    for (RowId row = 0; row < nRows_; ++row) {
        screen[row] = row;
    }
    history.clear();
    for (RowId row = nRows_; row < nRows_ + historyCount; ++row) {
        history.push_back(row);
    }
    freeRows.clear();
    for (RowId row = nRows_ + historyCount; row < nRows_ + saveLines; ++row) {
        freeRows.push_back(row);
    }

    if (!wasScrolled) {
        viewOffset = 0;
    } else if (viewAnchor.mapped.y >= (int)(retainedStart) && viewAnchor.mapped.y < (int)(screenStart)) {
        viewOffset = screenStart - viewAnchor.mapped.y;
    } else if (viewAnchor.mapped.y < (int)(retainedStart)) {
        viewOffset = historyCount;
    } else {
        viewOffset = 0;
    }
    if (keepSelection && selectionStart.mapped.y >= (int)(retainedStart) && selectionStart.mapped.y < (int)(screenStart + nRows_) && selectionEnd.mapped.y >= (int)(retainedStart) && selectionEnd.mapped.y < (int)(screenStart + nRows_)) {
        selection.tl = Point(selectionStart.mapped.x, selectionStart.mapped.y - screenStart);
        selection.br = Point(selectionEnd.mapped.x, selectionEnd.mapped.y - screenStart);
    } else {
        selection.clear();
    }

    if (cursorState != nullptr) {
        cursorState->pendingWrap = cursorAnchor.mapped.x == nCols_;
        cursorState->position.x = cursorState->pendingWrap ? nCols_ - 1 : std::min(cursorAnchor.mapped.x, (int)(nCols_ - 1));
        cursorState->position.y = std::max(0, std::min(cursorAnchor.mapped.y - (int)(screenStart), (int)(nRows_ - 1)));
    }
}

void ScreenImpl::fullCopyCells(RenderCell* const dst) const {
    CellExtraStore* const extras = cellExtras();
    assert(extras != nullptr);
    RenderCell* p = dst;
    for (int pY = 0; pY < nRows; ++pY) {
        const TerminalCell* src = getViewRowPtr(pY);
        for (u16 pX = 0; pX < nCols; ++pX) {
            *p++ = materialize(src[pX], *extras);
        }
    }
}

void ScreenImpl::deltaCopyCells(RenderCell* const dst) {
    CellExtraStore* const extras = cellExtras();
    assert(extras != nullptr);

    const u32* current = damage.cells;
    const u32* const end = current + damage.count;
    while (current != end) {
        const u32 packed = *current++;
        const u16 row = packed >> 16;
        const u16 column = packed & 0xffff;
        u16 count = 1;
        while (current != end && *current == packed + count) {
            ++current;
            ++count;
        }
        const size_t offset = (size_t)(row)*nCols + column;
        damageDeltaCopy(dst + offset, getViewRowPtr(row) + column, count, *extras);
    }
}

Rect ScreenImpl::getSelectionForView() const {
    if (!selectionValid()) {
        return {};
    }

    Rect ret = selection;
    if (!ret.null()) {
        ret.tl.y += viewOffset;
        ret.br.y += viewOffset;
    }
    return ret;
}

Rect ScreenImpl::getSnappedSelection() const {
    Rect ret = selection;

    if (ret.null()) {
        return ret;
    }
    if (!selectionValid()) {
        return {};
    }

    if (selection.rectangular) {
        ret.tl.y += viewOffset;
        ret.br.y += viewOffset;
        return ret;
    }

    switch (snapTo) {
        case SelectSnapTo::Char:
            break;
        case SelectSnapTo::Word: {
            const auto cellLead = [this](const TerminalCell* row, int x) {
                x = std::max(0, std::min(x, (int)(nCols)-1));
                return row[x].dwidth_cont && x > 0 ? x - 1 : x;
            };
            const auto expand = [this, &cellLead](int rowIndex, int x) {
                const auto* row = getLogicalRowPtr(rowIndex);
                int left = cellLead(row, x);
                const u32 selectedClass = wordClass(row[left].uc_pt ? row[left].uc_pt : ' ');
                while (left > 0) {
                    const int previous = cellLead(row, left - 1);
                    const u32 codepoint = row[previous].uc_pt ? row[previous].uc_pt : ' ';
                    if (wordClass(codepoint) != selectedClass) {
                        break;
                    }
                    left = previous;
                }

                int right = left;
                while (right < nCols) {
                    const int lead = cellLead(row, right);
                    const u32 codepoint = row[lead].uc_pt ? row[lead].uc_pt : ' ';
                    if (wordClass(codepoint) != selectedClass) {
                        break;
                    }
                    right = lead + (row[lead].dwidth ? 2 : 1);
                }
                return std::pair<int, int>{left, right};
            };

            ret.tl.x = expand(ret.tl.y, ret.tl.x).first;
            ret.br.x = expand(ret.br.y, ret.br.x).second;
        } break;
        case SelectSnapTo::Line:
            ret.tl.x = 0;
            ret.br.x = nCols;
            break;
        default:
            break;
    }

    ret.tl.y += viewOffset;
    ret.br.y += viewOffset;
    return ret;
}

bool ScreenImpl::getSelectedUtf8(std::string& utf8_selection) const {
    Rect sel = getSnappedSelection();

    if (sel.empty()) {
        return false;
    }
    sel.tl.y -= viewOffset;
    sel.br.y -= viewOffset;

    using unicodeString = std::vector<u32>;
    std::vector<unicodeString> lines;
    CellExtraStore* const extras = cellExtras();
    assert(extras != nullptr);
    bool wrap = false;

    auto addLine = [&](int y, u16 x1, u16 x2) {
        unicodeString line;
        size_t contentEnd = 0;
        bool wrapBack = wrap;
        wrap = false;
        const auto* cp = getLogicalRowPtr(y);
        for (u16 x = x1; x < x2; ++x) {
            const auto& cell = cp[x];
            if (!cell.dwidth_cont) {
                const auto grapheme = extras->grapheme(cell);
                if (grapheme.empty()) {
                    line.push_back(cell.uc_pt ? cell.uc_pt : ' ');
                } else {
                    line.insert(line.end(), grapheme.begin(), grapheme.end());
                }
                if (cell.drawn || (cell.uc_pt != 0 && cell.uc_pt != ' ') || !grapheme.empty()) {
                    contentEnd = line.size();
                }
            }
            if (cell.wrap) {
                wrap = true;
                break;
            }
        }

        // Trim screen padding only when a linear selection consumes the rest
        // of the row.  Explicitly selected whitespace (word or rectangle)
        // is data and must survive copying.
        if (!wrap && !sel.rectangular && x2 == nCols) {
            line.resize(contentEnd);
        }

        if (wrapBack && lines.size()) {
            lines.back().insert(lines.back().end(), line.begin(), line.end());
        } else {
            lines.push_back(line);
        }
    };

    if (sel.tl.y == sel.br.y) {
        addLine(sel.tl.y, sel.tl.x, sel.br.x);
    } else if (sel.rectangular) {
        for (int y = sel.tl.y; y <= sel.br.y; ++y) {
            addLine(y, sel.tl.x, sel.br.x);
        }
    } else {
        addLine(sel.tl.y, sel.tl.x, nCols);
        for (int y = sel.tl.y + 1; y < sel.br.y; ++y) {
            addLine(y, 0, nCols);
        }
        addLine(sel.br.y, 0, sel.br.x);
    }

    std::vector<char> utf8_out;
    auto sinkFn = [&](char ch) {
        utf8_out.push_back(ch);
    };
    for (const auto& codepoints : lines) {
        for (u32 cp : codepoints) {
            Utf8Encoder::pushUnicode(cp, sinkFn);
        }
        utf8_out.push_back('\n');
    }
    while (utf8_out.size() && utf8_out.back() == '\n') {
        utf8_out.pop_back();
    }

    utf8_selection = std::string(utf8_out.data(), utf8_out.size());

    return true;
}

RenderCell ScreenImpl::materialize(const TerminalCell& cell, const CellExtraStore& extras) const {
    assert(colors != nullptr);
    RenderCell result;
    result.uc_pt = cell.uc_pt ? cell.uc_pt : ' ';
    result.dwidth = cell.dwidth;
    result.dwidth_cont = cell.dwidth_cont;
    result.bold = cell.bold;
    result.italic = cell.italic;
    result.underline = cell.underlined();
    result.inverse = cell.inverse;
    result.wrap = cell.wrap;
    result.faint = cell.faint;
    result.blink = cell.blink;
    result.conceal = cell.conceal;
    result.strike = cell.strike;
    result.overline = cell.overline;
    result.underline_style = cell.underline_style;
    result.protected_char = cell.protected_char;
    result.drawn = cell.drawn;
    result.line_attr = cell.line_attr;
    result.fg = colors->resolveForeground(cell);
    result.bg = colors->resolveBackground(cell);
    const CellColor underlineColor = extras.underlineColor(cell);
    result.underline_color = colors->resolve(underlineColor);
    if (cell.underlined() && underlineColor == cell.foreground()) {
        result.underline_color = result.fg;
    }
    result.hyperlink = extras.hyperlinkDisplayId(cell);
    result.grapheme = extras.grapheme(cell).empty() ? 0 : cell.extraRef();
    result.semantic = cell.semantic;
    return result;
}

void ScreenImpl::damageDeltaCopy(RenderCell* dst, const TerminalCell* src, u16 count, const CellExtraStore& extras) const {
    for (u16 index = 0; index < count; ++index) {
        RenderCell rendered = materialize(src[index], extras);
        const bool wasDirty = dst[index].dirty;
        dst[index].dirty = 0;
        if (dst[index] != rendered) {
            dst[index] = rendered;
            dst[index].dirty = 1;
        } else {
            dst[index].dirty = wasDirty;
        }
    }
}

void ScreenImpl::setCursorPos(u16 pY, u16 pX) {
    cursor.posY = pY;
    cursor.posX = pX;
}

TerminalCursor ScreenImpl::getCursor() const {
    TerminalCursor ret = cursor;
    ret.posY += viewOffset;
    return ret;
}

Point ScreenImpl::getLogicalPoint(Point point) const {
    point.y -= viewOffset;
    return point;
}

void ScreenImpl::setCursorStyle(TerminalCursor::Style cs) {
    cursor.style = cs;
}

void ScreenImpl::setCursorColor(Color color) {
    cursor.color = color;
}

void ScreenImpl::pageUp(u16 count) {
    u16 viewOffset_ = std::min<size_t>(viewOffset + count, history.size());
    viewOffset = viewOffset_;
    expose();
}

void ScreenImpl::pageDown(u16 count) {
    u16 viewOffset_ = std::max(0, viewOffset - count);
    viewOffset = viewOffset_;
    expose();
}

bool ScreenImpl::pageToBottom() {
    if (!viewOffset) {
        return false;
    }

    viewOffset = 0;
    expose();
    return true;
}

void ScreenImpl::scrollUp(u16 top, u16 bottom, u16 count) {
    count = std::min<u16>(count, bottom - top);
    const bool capture = top == 0 && saveLines;
    const u16 previousViewOffset = viewOffset;
    if (!capture) {
        vscrollSelection(top, bottom, -count, false);
    }

    for (u16 k = 0; k < count; ++k) {
        const RowId outgoing = screen[top];
        RowId incoming = outgoing;

        if (capture) {
            if (history.size() == saveLines) {
                incoming = history.front();
                history.pop_front();
            } else {
                incoming = freeRows.back();
                freeRows.pop_back();
            }
            history.push_back(outgoing);
        }

        for (u16 row = top; row + 1 < bottom; ++row) {
            screen[row] = screen[row + 1];
        }
        screen[bottom - 1] = incoming;
    }

    if (capture) {
        vscrollSelection(top, bottom, -count, true);
    }

    if (capture && viewOffset) {
        viewOffset = std::min<size_t>(viewOffset + count, history.size());
    }
    if (capture && previousViewOffset) {
        expose();
    } else {
        damageRectangle(top, 0, bottom, nCols);
    }
}

void ScreenImpl::scrollDown(u16 top, u16 bottom, u16 count) {
    count = std::min<u16>(count, bottom - top);
    vscrollSelection(top, bottom, count, false);

    for (u16 k = 0; k < count; ++k) {
        const RowId incoming = screen[bottom - 1];
        for (u16 row = bottom - 1; row > top; --row) {
            screen[row] = screen[row - 1];
        }
        screen[top] = incoming;
    }

    damageRectangle(top, 0, bottom, nCols);
}

void ScreenImpl::restoreHistory(u16 count) {
    count = std::min<size_t>(count, history.size());
    for (u16 k = 0; k < count; ++k) {
        const RowId incoming = history.back();
        history.pop_back();
        const RowId outgoing = screen.back();
        for (u16 row = nRows - 1; row > 0; --row) {
            screen[row] = screen[row - 1];
        }
        screen[0] = incoming;
        freeRows.push_back(outgoing);
    }
    viewOffset = viewOffset > count ? viewOffset - count : 0;
    if (!selection.null()) {
        selection.tl.y += count;
        selection.br.y += count;
    }
    expose();
}

TerminalCell* ScreenImpl::dirtySpan(u16 pY, u16 startX, u16 count) {
    const u32 idx = getIdx(pY, startX);
    damageRow(pY, startX, startX + count);
    if (!selection.empty()) {
        invalidateSelection(Rect(startX, pY, startX + count, pY));
    }
    return cells.get() + idx;
}

void ScreenImpl::fillCells(u16 ch, const TerminalCell& attrs) {
    for (u16 r = 0; r < nRows; ++r) {
        u32 start = getIdx(r, 0);
        u32 end = start + nCols;
        for (u32 k = start; k < end; ++k) {
            cells.get()[k] = attrs;
            cells.get()[k].uc_pt = ch == ' ' ? 0 : ch;
            cells.get()[k].drawn = ch != ' ';
        }
    }
    damageRectangle(0, 0, nRows, nCols);
}

void ScreenImpl::setLineAttribute(u16 row, u8 attribute) {
    TerminalCell* cells_ = dirtySpan(row, 0, nCols);
    for (u16 column = 0; column < nCols; ++column) {
        cells_[column].line_attr = attribute;
    }
}

u8 ScreenImpl::lineAttribute(u16 row) const noexcept {
    return getLogicalRowPtr(row)[0].line_attr;
}

bool ScreenImpl::hasProtection(u16 row, u8 mask) const noexcept {
    const TerminalCell* cells_ = getLogicalRowPtr(row);
    for (u16 column = 0; column < nCols; ++column) {
        if ((cells_[column].protected_char & mask) != 0) {
            return true;
        }
    }
    return false;
}

bool ScreenImpl::wrapped(u16 row, u16 column) const noexcept {
    return getLogicalRowPtr(row)[column].wrap;
}

void ScreenImpl::setWrapped(u16 row, u16 column) {
    TerminalCell* cells_ = cells.get() + getIdx(row, 0);
    cells_[column].wrap = 1;
    damageCell(row, column);
    if (!selection.empty()) {
        invalidateSelection(Rect(column, row));
    }
}

void ScreenImpl::moveWrap(u16 row, u16 sourceColumn, u16 destinationColumn) {
    const TerminalCell* cells_ = getLogicalRowPtr(row);
    if (!cells_[sourceColumn].wrap || sourceColumn == destinationColumn) {
        return;
    }
    TerminalCell* mutableCells = cells.get() + getIdx(row, 0);
    mutableCells[sourceColumn].wrap = 0;
    mutableCells[destinationColumn].wrap = 1;
    damageCell(row, sourceColumn);
    damageCell(row, destinationColumn);
    if (!selection.empty()) {
        const u16 begin = sourceColumn < destinationColumn ? sourceColumn : destinationColumn;
        const u16 end = sourceColumn > destinationColumn ? sourceColumn + 1 : destinationColumn + 1;
        invalidateSelection(Rect(begin, row, end, row));
    }
}

TerminalCell* ScreenImpl::prepareSpan(u16 row, u16 start, u16 count, const TerminalCell& eraseAttrs) {
    const u16 end = start + count;
    const TerminalCell* cells_ = getLogicalRowPtr(row);
    const bool splitLeft = start > 0 && (cells_[start - 1].dwidth || cells_[start].dwidth_cont);
    const bool splitRight = end < nCols && (cells_[end - 1].dwidth || cells_[end].dwidth_cont);
    if (!splitLeft && !splitRight) {
        return dirtySpan(row, start, count);
    }
    return overwriteWideSpan(row, start, count, eraseAttrs);
}

TerminalCell& ScreenImpl::prepareCell(u16 row, u16 column, const TerminalCell& eraseAttrs) {
    const TerminalCell* cells_ = getLogicalRowPtr(row);
    if (cells_[column].dwidth_cont) {
        clearWideBoundary(row, column, eraseAttrs);
    } else if (cells_[column].dwidth) {
        clearWideBoundary(row, column + 1, eraseAttrs);
    }
    damageCell(row, column);
    if (!selection.empty()) {
        invalidateSelection(Rect(column, row));
    }
    return cells.get()[getIdx(row, column)];
}

void ScreenImpl::writeGrapheme(u16 row, u16 column, const u32* codepoints, size_t count, bool wide, const TerminalCell& attrs, u32 hyperlink, u32 semantic, u8 lineAttribute_, const TerminalCell& eraseAttrs) {
    assert(count != 0);
    TerminalCell lead = attrs;
    lead.uc_pt = codepoints[0];
    lead.drawn = 1;
    lead.dwidth = wide;
    lead.semantic = semantic;
    lead.line_attr = lineAttribute_;
    CellExtraStore* const extras = cellExtras();
    extras->setHyperlink(lead, hyperlink);
    if (count > 1) {
        extras->setGrapheme(lead, codepoints, count);
    }
    prepareCell(row, column, eraseAttrs) = lead;

    if (!wide) {
        return;
    }

    TerminalCell continuation = attrs;
    continuation.dwidth_cont = 1;
    continuation.drawn = 1;
    continuation.semantic = semantic;
    continuation.line_attr = lineAttribute_;
    extras->setHyperlink(continuation, lead.extraRef());
    prepareCell(row, column + 1, eraseAttrs) = continuation;
}

void ScreenImpl::writeAsciiRun(u16 row, u16 column, const u8* input, u16 count, const TerminalCell& attrs, u32 hyperlink, u32 semantic, u8 lineAttribute_, const TerminalCell& eraseAttrs) {
    TerminalCell linkedAttrs = attrs;
    cellExtras()->setHyperlink(linkedAttrs, hyperlink);
    TerminalCell* cells_ = prepareSpan(row, column, count, eraseAttrs);
    for (u16 index = 0; index < count; ++index) {
        TerminalCell& cell = cells_[index];
        cell = linkedAttrs;
        cell.uc_pt = input[index];
        cell.drawn = 1;
        cell.semantic = semantic;
        cell.line_attr = lineAttribute_;
    }
}

void ScreenImpl::fillRectangle(u16 top, u16 left, u16 bottom, u16 right, u32 codepoint, const TerminalCell& attrs, const TerminalCell& eraseAttrs) {
    for (u16 row = top; row < bottom; ++row) {
        clearWideBoundary(row, left, eraseAttrs);
        clearWideBoundary(row, right, eraseAttrs);
        TerminalCell* cells_ = cells.get() + getIdx(row, left);
        for (u16 column = left; column < right; ++column) {
            TerminalCell& cell = cells_[column - left];
            const u8 lineAttribute_ = cell.line_attr;
            cell = attrs;
            cell.line_attr = lineAttribute_;
            cell.uc_pt = codepoint;
        }
    }
    damageRectangle(top, left, bottom, right);
    if (!selection.empty()) {
        invalidateSelection(Rect(left, top, right, bottom));
    }
}

void ScreenImpl::copyRectangle(u16 sourceTop, u16 sourceLeft, u16 targetTop, u16 targetLeft, u16 height, u16 width, const TerminalCell& eraseAttrs) {
    std::vector<TerminalCell> copied;
    copied.reserve((size_t)(height)*width);
    for (u16 row = 0; row < height; ++row) {
        const TerminalCell* source = getLogicalRowPtr(sourceTop + row) + sourceLeft;
        copied.insert(copied.end(), source, source + width);
    }
    for (u16 row = 0; row < height; ++row) {
        clearWideBoundary(targetTop + row, targetLeft, eraseAttrs);
        clearWideBoundary(targetTop + row, targetLeft + width, eraseAttrs);
        TerminalCell* destination = cells.get() + getIdx(targetTop + row, targetLeft);
        for (u16 column = 0; column < width; ++column) {
            const u8 lineAttribute_ = destination[column].line_attr;
            destination[column] = copied[(size_t)(row)*width + column];
            destination[column].line_attr = lineAttribute_;
        }
        repairWideBoundary(targetTop + row, targetLeft, eraseAttrs);
        repairWideBoundary(targetTop + row, targetLeft + width, eraseAttrs);
    }
    damageRectangle(targetTop, targetLeft, targetTop + height, targetLeft + width);
    if (!selection.empty()) {
        invalidateSelection(Rect(targetLeft, targetTop, targetLeft + width, targetTop + height));
    }
}

void ScreenImpl::changeRectangleAttributes(u16 top, u16 left, u16 bottom, u16 right, const u32* modes, size_t modeCount, bool reverse) {
    const auto apply = [reverse](TerminalCell& cell, u32 mode) {
        switch (mode) {
            case 0:
                cell.bold = reverse ? !cell.bold : false;
                cell.underline_style = reverse ? !cell.underlined() : 0;
                cell.blink = reverse ? !cell.blink : false;
                cell.inverse = reverse ? !cell.inverse : false;
                break;
            case 1:
                cell.bold = reverse ? !cell.bold : true;
                break;
            case 4:
                cell.underline_style = reverse ? !cell.underlined() : 1;
                break;
            case 5:
                cell.blink = reverse ? !cell.blink : true;
                break;
            case 7:
                cell.inverse = reverse ? !cell.inverse : true;
                break;
            case 8:
                cell.conceal = reverse ? !cell.conceal : true;
                break;
            case 22:
                if (!reverse) {
                    cell.bold = 0;
                }
                break;
            case 24:
                if (!reverse) {
                    cell.underline_style = 0;
                }
                break;
            case 25:
                if (!reverse) {
                    cell.blink = 0;
                }
                break;
            case 27:
                if (!reverse) {
                    cell.inverse = 0;
                }
                break;
            case 28:
                if (!reverse) {
                    cell.conceal = 0;
                }
                break;
        }
    };

    for (u16 row = top; row < bottom; ++row) {
        TerminalCell* cells_ = cells.get() + getIdx(row, left);
        for (u16 column = left; column < right; ++column) {
            TerminalCell& cell = cells_[column - left];
            for (size_t index = 0; index < modeCount; ++index) {
                apply(cell, modes[index]);
            }
        }
    }
    damageRectangle(top, left, bottom, right);
    if (!selection.empty()) {
        invalidateSelection(Rect(left, top, right, bottom));
    }
}

u16 ScreenImpl::checksum(u16 top, u16 left, u16 bottom, u16 right) const noexcept {
    u16 result = 0;
    for (u16 row = top; row < bottom; ++row) {
        const TerminalCell* cells_ = getLogicalRowPtr(row);
        for (u16 column = left; column < right; ++column) {
            if (cells_[column].uc_pt != ' ') {
                result += cells_[column].uc_pt & 0xff;
            }
        }
    }
    return -result;
}

void ScreenImpl::appendPrintableLine(u16 row, std::string& output) const {
    std::vector<u32> codepoints;
    const TerminalCell* cells_ = getLogicalRowPtr(row);
    CellExtraStore* const extras = cellExtras();
    for (u16 column = 0; column < nCols; ++column) {
        const TerminalCell& cell = cells_[column];
        if (cell.dwidth_cont) {
            continue;
        }
        const GraphemeView grapheme = extras->grapheme(cell);
        if (grapheme.empty()) {
            codepoints.push_back(cell.uc_pt ? cell.uc_pt : ' ');
        } else {
            codepoints.insert(codepoints.end(), grapheme.begin(), grapheme.end());
        }
    }
    while (!codepoints.empty() && codepoints.back() == ' ') {
        codepoints.pop_back();
    }
    const auto sink = [&output](char ch) {
        output.push_back(ch);
    };
    for (u32 codepoint : codepoints) {
        Utf8Encoder::pushUnicode(codepoint, sink);
    }
    output.push_back('\n');
}

StringView ScreenImpl::hyperlinkAt(u16 row, u16 column) const noexcept {
    return cellExtras()->hyperlink(getViewRowPtr(row)[column]);
}

TerminalCell ScreenImpl::testCell(u16 row, u16 column) const noexcept {
    return getViewRowPtr(row)[column];
}

void ScreenImpl::eraseInRow(u16 pY, u16 startX, u16 count, const TerminalCell& attrs) {
    if (!count) {
        return;
    }

    u32 idx = getIdx(pY, startX);
    TerminalCell erased = attrs;
    erased.line_attr = getLogicalRowPtr(pY)[0].line_attr;
    if (startX == 0 && count == nCols) {
        if (!erasedRowTemplateValid || erasedRowTemplate.size() != nCols || erasedRowCell != erased) {
            erasedRowTemplate.assign(nCols, erased);
            erasedRowCell = erased;
            erasedRowTemplateValid = true;
        }
        memcpy(cells.get() + idx, erasedRowTemplate.data(), nCols * cellSize);
    } else {
        eraseRange(idx, idx + count, erased);
    }
    damageRow(pY, startX, startX + count);
    if (!selection.empty()) {
        invalidateSelection(Rect(startX, pY, startX + count, pY));
    }
}

void ScreenImpl::eraseWideInRow(u16 pY, u16 startX, u16 count, const TerminalCell& attrs) {
    if (!count) {
        return;
    }
    const u16 endX = startX + count;
    const u32 rowIdx = getIdx(pY, 0);
    TerminalCell* row = cells.get() + rowIdx;
    TerminalCell erased = attrs;
    erased.line_attr = row[0].line_attr;
    const bool eraseLeft = startX > 0 && (row[startX - 1].dwidth || row[startX].dwidth_cont);
    const bool eraseRight = endX < nCols && (row[endX - 1].dwidth || row[endX].dwidth_cont);
    if (eraseLeft) {
        row[startX - 1] = erased;
    }
    if (eraseRight) {
        row[endX] = erased;
    }
    if (startX == 0 && count == nCols) {
        if (!erasedRowTemplateValid || erasedRowTemplate.size() != nCols || erasedRowCell != erased) {
            erasedRowTemplate.assign(nCols, erased);
            erasedRowCell = erased;
            erasedRowTemplateValid = true;
        }
        memcpy(row, erasedRowTemplate.data(), nCols * cellSize);
    } else {
        for (u16 x = startX; x < endX; ++x) {
            row[x] = erased;
        }
    }
    const u16 damageStart = eraseLeft ? startX - 1 : startX;
    const u16 damageEnd = eraseRight ? endX + 1 : endX;
    damageRow(pY, damageStart, damageEnd);
    if (!selection.empty()) {
        invalidateSelection(Rect(damageStart, pY, damageEnd, pY));
    }
}

TerminalCell* ScreenImpl::overwriteWideSpan(u16 pY, u16 startX, u16 count, const TerminalCell& eraseAttrs) {
    const u16 endX = startX + count;
    const u32 rowIdx = getIdx(pY, 0);
    TerminalCell* row = cells.get() + rowIdx;
    TerminalCell erased = eraseAttrs;
    erased.line_attr = row[0].line_attr;
    const bool eraseLeft = startX > 0 && (row[startX - 1].dwidth || row[startX].dwidth_cont);
    const bool eraseRight = endX < nCols && (row[endX - 1].dwidth || row[endX].dwidth_cont);
    if (eraseLeft) {
        row[startX - 1] = erased;
    }
    if (eraseRight) {
        row[endX] = erased;
    }
    const u16 damageStart = eraseLeft ? startX - 1 : startX;
    const u16 damageEnd = eraseRight ? endX + 1 : endX;
    damageRow(pY, damageStart, damageEnd);
    if (!selection.empty()) {
        invalidateSelection(Rect(damageStart, pY, damageEnd, pY));
    }
    return row + startX;
}

void ScreenImpl::clearWideBoundary(u16 pY, u16 boundary, const TerminalCell& attrs) {
    const u32 rowIdx = getIdx(pY, 0);
    TerminalCell* row = cells.get() + rowIdx;
    const bool eraseLeft = boundary > 0 && row[boundary - 1].dwidth;
    const bool eraseRight = boundary < nCols && row[boundary].dwidth_cont;
    if (!eraseLeft && !eraseRight) {
        return;
    }
    TerminalCell erased = attrs;
    erased.line_attr = row[0].line_attr;
    if (eraseLeft) {
        row[boundary - 1] = erased;
        damageCell(pY, boundary - 1);
        if (!selection.empty()) {
            invalidateSelection(Rect(boundary - 1, pY));
        }
    }
    if (eraseRight) {
        row[boundary] = erased;
        damageCell(pY, boundary);
        if (!selection.empty()) {
            invalidateSelection(Rect(boundary, pY));
        }
    }
}

void ScreenImpl::repairWideBoundary(u16 pY, u16 boundary, const TerminalCell& attrs) {
    const u32 rowIdx = getIdx(pY, 0);
    TerminalCell* row = cells.get() + rowIdx;
    const bool leftLead = boundary > 0 && row[boundary - 1].dwidth;
    const bool rightContinuation = boundary < nCols && row[boundary].dwidth_cont;
    if (leftLead == rightContinuation) {
        return;
    }
    TerminalCell erased = attrs;
    erased.line_attr = row[0].line_attr;
    if (leftLead) {
        row[boundary - 1] = erased;
        damageCell(pY, boundary - 1);
        if (!selection.empty()) {
            invalidateSelection(Rect(boundary - 1, pY));
        }
    } else {
        row[boundary] = erased;
        damageCell(pY, boundary);
        if (!selection.empty()) {
            invalidateSelection(Rect(boundary, pY));
        }
    }
}

void ScreenImpl::selectiveEraseInRow(u16 pY, u16 startX, u16 count, const TerminalCell& attrs, u8 protectionMask) {
    CellExtraStore* const extras = cellExtras();
    TerminalCell erased = attrs;
    erased.uc_pt = 0;
    erased.protected_char = 0;
    extras->clearExtra(erased, extras->underlineColor(attrs));
    TerminalCell* row = cells.get() + getIdx(pY, 0);
    bool changed = false;
    u16 changedStart = nCols;
    for (u16 x = startX; x < startX + count; ++x) {
        TerminalCell& cell = row[x];
        if (!(cell.protected_char & protectionMask)) {
            if (changedStart == nCols) {
                changedStart = x;
            }
            erased.line_attr = cell.line_attr;
            cell = erased;
            changed = true;
        } else if (changedStart != nCols) {
            if (!selection.empty()) {
                invalidateSelection(Rect(changedStart, pY, x, pY));
            }
            changedStart = nCols;
        }
    }
    if (changedStart != nCols && !selection.empty()) {
        invalidateSelection(Rect(changedStart, pY, startX + count, pY));
    }
    if (changed) {
        damageRow(pY, startX, startX + count);
    }
}

void ScreenImpl::moveInRow(u16 pY, u16 dstX, u16 srcX, u16 count) {
    if (!count) {
        return;
    }

    u32 dstIdx = getIdx(pY, dstX);
    u32 srcIdx = getIdx(pY, srcX);
    moveCells(dstIdx, srcIdx, count);
    damageRow(pY, dstX, dstX + count);
    if (!selection.empty()) {
        invalidateSelection(Rect(dstX, pY, dstX + count, pY));
    }
}

void ScreenImpl::copyRow(u16 dstY, u16 srcY, u16 startX, u16 count) {
    if (!count) {
        return;
    }

    u32 dstIdx = getIdx(dstY, startX);
    u32 srcIdx = getIdx(srcY, startX);
    copyCells(dstIdx, srcIdx, count);
    damageRow(dstY, startX, startX + count);
    if (!selection.empty()) {
        invalidateSelection(Rect(startX, dstY, startX + count, dstY));
    }
}

void ScreenImpl::rotateRowsUp(u16 top, u16 bottom, u16 count) {
    count = std::min<u16>(count, bottom - top);
    if (!count) {
        return;
    }
    if (!selection.empty()) {
        invalidateSelection(Rect(0, top, 0, bottom));
    }
    std::rotate(screen.begin() + top, screen.begin() + top + count, screen.begin() + bottom);
    damageRectangle(top, 0, bottom, nCols);
}

void ScreenImpl::rotateRowsDown(u16 top, u16 bottom, u16 count) {
    count = std::min<u16>(count, bottom - top);
    if (!count) {
        return;
    }
    if (!selection.empty()) {
        invalidateSelection(Rect(0, top, 0, bottom));
    }
    std::rotate(screen.begin() + top, screen.begin() + bottom - count, screen.begin() + bottom);
    damageRectangle(top, 0, bottom, nCols);
}

void ScreenImpl::invalidateSelection(const Rect&& damage) {
    if (selection.empty()) {
        return;
    }

    if (selection.rectangular) {
        const bool outsideRows = damage.tl.y > selection.br.y || damage.br.y < selection.tl.y;
        const bool outsideColumns = damage.br.x <= selection.tl.x || selection.br.x <= damage.tl.x;
        if (outsideRows || outsideColumns) {
            return;
        }
        selection.clear();
        return;
    }

    if (selection.br <= damage.tl || damage.br <= selection.tl) {
        return;
    }

    selection.clear();
}

bool ScreenImpl::selectionValid() const {
    if (selection.null()) {
        return true;
    }

    const int firstRow = -(int)(history.size());
    const auto valid = [&](Point point) {
        return point.x >= 0 && point.x <= nCols && point.y >= firstRow && point.y < nRows;
    };
    return valid(selection.tl) && valid(selection.br);
}

void ScreenImpl::vscrollSelection(u16 top, u16 bottom, int vertOffset, bool captureHistory) {
    if (selection.null()) {
        return;
    }

    if (captureHistory) {
        if (selection.tl.y >= bottom) {
            return;
        }
        if (selection.br.y >= bottom) {
            selection.clear();
            return;
        }
        selection.tl.y += vertOffset;
        selection.br.y += vertOffset;
        if (selection.tl.y < -(int)(history.size())) {
            selection.clear();
        }
        return;
    }

    const bool topInside = selection.tl.y >= top && selection.tl.y < bottom;
    const bool bottomInside = selection.br.y >= top && selection.br.y < bottom;
    if (!topInside && !bottomInside) {
        if (selection.br.y < top || selection.tl.y >= bottom) {
            return;
        }
        selection.clear();
        return;
    }
    if (topInside != bottomInside) {
        selection.clear();
        return;
    }

    selection.tl.y += vertOffset;
    selection.br.y += vertOffset;
    if (selection.tl.y < top || selection.br.y >= bottom) {
        selection.clear();
    }
}

ScreenImpl::RowId ScreenImpl::getLogicalRow(int pY) const {
    if (pY < 0) {
        const int index = (int)(history.size()) + pY;
        return history[(size_t)(index)];
    }
    return screen[pY];
}

const TerminalCell* ScreenImpl::getLogicalRowPtr(int pY) const {
    return &operator[](nCols* getLogicalRow(pY));
}

const TerminalCell* ScreenImpl::getViewRowPtr(int pY) const {
    return getLogicalRowPtr(pY - viewOffset);
}

u32 ScreenImpl::getIdx(u16 pY, u16 pX) const {
    return nCols * screen[pY] + pX;
}

const TerminalCell& ScreenImpl::operator[](u32 idx) const {
    return cells.get()[idx];
}

TerminalCell& ScreenImpl::operator[](u32 idx) {
    return cells.get()[idx];
}

void ScreenImpl::eraseRange(u32 start, u32 end, const TerminalCell& attrs) {
    TerminalCell* ca = &(cells.get()[start]);
    TerminalCell* const cz = ca - start + end;
    while (ca < cz) {
        *ca++ = attrs;
    }
}

void ScreenImpl::copyCells(u32 dstIx, u32 srcIx, u32 count) {
    memcpy(cells.get() + dstIx, cells.get() + srcIx, count * cellSize);
}

void ScreenImpl::moveCells(u32 dstIx, u32 srcIx, u32 count) {
    memmove(cells.get() + dstIx, cells.get() + srcIx, count * cellSize);
}

void ScreenImpl::damageCell(u16 row, u16 column) {
    const u32 viewRow = (u32)(row) + viewOffset;
    if (viewRow < nRows) {
        damage.addCell(viewRow, column);
    }
}

void ScreenImpl::damageRow(u16 row, u16 begin, u16 end) {
    const u32 viewRow = (u32)(row) + viewOffset;
    if (viewRow < nRows) {
        damage.addRow(viewRow, begin, end);
    }
}

void ScreenImpl::damageRectangle(u16 top, u16 left, u16 bottom, u16 right) {
    const u32 viewTop = (u32)(top) + viewOffset;
    const u32 viewBottom = (u32)(bottom) + viewOffset;
    if (viewTop >= nRows) {
        return;
    }
    damage.addRect(viewTop, left, viewBottom < nRows ? viewBottom : nRows, right);
}

void ScreenImpl::resizeDamage(u16 columns, u16 rows) {
    const size_t count = (size_t)(columns)*rows;
    damageStorage.grow(count * sizeof(u32) * 2);
    damage.configure(damageStorage.mutData(), columns, rows);
}

ScreenImpl::Damage& ScreenImpl::Damage::operator=(Damage&& other) noexcept {
    cells = other.cells;
    epochs = other.epochs;
    capacity = other.capacity;
    count = other.count;
    epoch = other.epoch;
    width = other.width;
    height = other.height;
    other.cells = nullptr;
    other.epochs = nullptr;
    other.capacity = 0;
    other.count = 0;
    return *this;
}

void ScreenImpl::Damage::configure(void* storage, u16 columns, u16 rows_) {
    width = columns;
    height = rows_;
    const size_t required = (size_t)(width)*height;
    cells = static_cast<u32*>(storage);
    epochs = cells + required;
    capacity = required;
    if (required != 0) {
        memset(epochs, 0, required * sizeof(u32));
    }
    count = 0;
    epoch = 1;
}

void ScreenImpl::Damage::reset() {
    count = 0;
    ++epoch;
    if (epoch == 0) {
        memset(epochs, 0, (size_t)(width)*height * sizeof(u32));
        epoch = 1;
    }
}

void ScreenImpl::Damage::expose() {
    reset();
    addRect(0, 0, height, width);
}

bool ScreenImpl::Damage::hasDamage() const noexcept {
    return count != 0;
}

void ScreenImpl::Damage::addCell(u16 row, u16 column) {
    if (count == capacity) {
        return;
    }
    const size_t index = (size_t)(row)*width + column;
    u32& cellEpoch = epochs[index];
    if (cellEpoch == epoch) {
        return;
    }
    cellEpoch = epoch;
    cells[count++] = ((u32)(row) << 16) | column;
}

void ScreenImpl::Damage::addRow(u16 row, u16 begin, u16 end) {
    if (end <= begin || count == capacity) {
        return;
    }
    const size_t rowOffset = (size_t)(row)*width;
    u32* const cellEpochs = epochs + rowOffset;
    for (u16 column = begin; column < end; ++column) {
        if (cellEpochs[column] != epoch) {
            cellEpochs[column] = epoch;
            cells[count++] = ((u32)(row) << 16) | column;
        }
    }
}

void ScreenImpl::Damage::addRect(u16 top, u16 left, u16 bottom, u16 right) {
    if (bottom <= top || right <= left || count == capacity) {
        return;
    }
    for (u16 row = top; row < bottom; ++row) {
        addRow(row, left, right);
    }
}
