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

#include <std/alg/minmax.h>
#include <std/dbg/assert.h>
#include <std/lib/buffer.h>
#include <std/mem/obj_pool.h>

#include <utf8proc.h>

#include <algorithm>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

using namespace stl;

// The complete geometry-independent state of one screen: content in its
// source geometry (storage borrowed until the old pool is destroyed),
// view/selection anchors, and the
// presentation scalars the terminal does not re-push after a rebuild.
struct ResizeState {
    u16 columns = 0;
    u16 rows = 0;
    u16 saveLines = 0;
    bool active = false;
    TerminalCell** rowRing = nullptr;
    const TerminalCell* zeroRow = nullptr;
    u32 rowCapacity = 0;
    u32 rowEnd = 0;
    u32 historyRows = 0;
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

    // Coord holds one grid coordinate and Epoch a damage generation counter.
    // The factories pick an instantiation from the actual geometry; resize
    // rebuilds convert between instantiations through ResizeState.
    template <typename Coord, typename Epoch>
    struct ScreenImpl final: public Screen {
        ScreenImpl(Composer& composer, ObjPool& pool);
        ScreenImpl(Composer& composer, ObjPool& pool, u16 columns, u16 rows, const TerminalColors* colors, u16 saveLines);

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
        void writeRun(u16 row, u16 column, const u32* codepoints, u16 count, const TerminalCell& attrs, u32 hyperlink, u32 semantic, u8 lineAttribute, const TerminalCell& eraseAttrs) override;
        void fillRectangle(u16 top, u16 left, u16 bottom, u16 right, u32 codepoint, const TerminalCell& attrs, const TerminalCell& eraseAttrs) override;
        void copyRectangle(u16 sourceTop, u16 sourceLeft, u16 targetTop, u16 targetLeft, u16 height, u16 width, const TerminalCell& eraseAttrs) override;
        void changeRectangleAttributes(u16 top, u16 left, u16 bottom, u16 right, const u32* modes, size_t modeCount, bool reverse) override;
        u16 checksum(u16 top, u16 left, u16 bottom, u16 right) const noexcept override;
        void appendPrintableLine(u16 row, std::string& output) const override;
        ScreenHyperlink hyperlinkAt(u16 row, u16 column) const override;
        TerminalCell testCell(u16 row, u16 column) const noexcept override;
        void fullCopyCells(RenderCell* dest) const override;
        void deltaCopyCells(RenderCell* dest) override;

        bool active() const noexcept override;

        CellExtraStore& cellExtras() const noexcept override;
        void collectExtraCells(Vector<TerminalCell*>& cells) override;
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

        using Packed = std::conditional_t<sizeof(Coord) == 1, u16, u32>;
        static constexpr unsigned packShift = sizeof(Coord) * 8;

        Coord nCols = 0;
        Coord nRows = 0;
        u32 saveLines = 0;
        u32 viewOffset = 0;
        u32 rowCapacity = 0;
        u32 rowEnd = 0;
        u32 historyRows = 0;
        u32 lineAttrRows = 0;
        TerminalCell** rowRing = nullptr;
        TerminalCell* zeroRow = nullptr;
        const TerminalColors* colors = nullptr;
        Composer& composer;
        ObjPool& pool;
        std::vector<TerminalCell> erasedRowTemplate;
        TerminalCell erasedRowCell{};
        bool erasedRowTemplateValid = false;
        Vector<TerminalCell*> freeRows;
        TerminalCursor cursor;
        Rect selection;
        Color selectionForeground = opts.fg;
        Color selectionBackground = opts.bg;
        u8 selectionColorMask = 0;
        bool blinkVisible = true;
        bool cursorBlink = false;
        bool screenReverseVideo = false;
        SelectSnapTo snapTo = SelectSnapTo::Char;
        Buffer damageStorage;

        struct LinkPosition {
            i32 row = 0;
            u16 column = 0;
        };

        struct LinkPart {
            LinkPosition position;
            size_t begin = 0;
            size_t end = 0;
        };

        mutable Vector<LinkPosition> linkLeft;
        mutable Vector<LinkPart> linkParts;
        mutable Buffer linkScratch;

        struct Damage {
            Packed* cells = nullptr;
            Epoch* epochs = nullptr;
            size_t capacity = 0;
            size_t count = 0;
            Epoch epoch = 0;
            Coord width = 0;
            Coord height = 0;
            bool full = false;

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

        u32 wrapRow(i64 row) const noexcept;
        TerminalCell*& logicalRowSlot(int row);
        TerminalCell* rawLogicalRow(int row) const noexcept;
        const TerminalCell* getLogicalRowPtr(int row) const;
        const TerminalCell* getViewRowPtr(int row) const;
        TerminalCell* mutableLogicalRow(int row);
        TerminalCell* allocateRow();
        void releaseRow(TerminalCell* row);
        void initializeRows(u16 columns, u16 rows, u16 history);
        void installRow(int row, const TerminalCell* source, u16 sourceColumns);
        static bool emptyRow(const TerminalCell* row, u16 columns);

        void eraseRange(TerminalCell* start, TerminalCell* end, const TerminalCell& attrs);
        void copyCells(TerminalCell* destination, const TerminalCell* source, u32 count);
        void moveCells(TerminalCell* destination, const TerminalCell* source, u32 count);
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

    u32 wrapStateRow(const ResizeState& state, i64 row) {
        i64 result = row % state.rowCapacity;
        if (result < 0) {
            result += state.rowCapacity;
        }
        return (u32)(result);
    }

    const TerminalCell* stateRowPtr(const ResizeState& state, int row) {
        const u32 slot = wrapStateRow(state, (i64)(state.rowEnd) - state.rows + row);
        const TerminalCell* const result = state.rowRing[slot];
        return result != nullptr ? result : state.zeroRow;
    }

}

template <typename Coord, typename Epoch>
ScreenImpl<Coord, Epoch>::ScreenImpl(Composer& composer_, ObjPool& pool_)
    : composer(composer_)
    , pool(pool_)
{
}

namespace {

    using SmallScreen = ScreenImpl<u8, u16>;
    using LargeScreen = ScreenImpl<u16, u32>;

    constexpr bool smallScreenGeometry(u32 columns, u32 rows, u32) {
        return columns <= 0xff && rows <= 0xff;
    }

    template <typename Impl>
    Impl* makeScreen(Composer& composer, ObjPool& pool) {
        return pool.make<Impl>(composer, pool);
    }

    template <typename Impl>
    Screen* makeScreenFromState(Composer& composer, ObjPool& pool, ResizeState& state, u16 columns, u16 rows, const TerminalColors* colors, bool reflow, Screen::Cursor* cursor) {
        Impl* const result = makeScreen<Impl>(composer, pool);
        if (state.active) {
            result->layout(state, columns, rows, colors, reflow, cursor);
        }
        return result;
    }

}

Screen* Screen::create(Composer& composer, ObjPool& pool) {
    return makeScreen<SmallScreen>(composer, pool);
}

Screen* Screen::create(Composer& composer, ObjPool& pool, u16 columns, u16 rows, const TerminalColors* colors, u16 saveLines) {
    if (smallScreenGeometry(columns, rows, saveLines)) {
        return pool.make<SmallScreen>(composer, pool, columns, rows, colors, saveLines);
    }
    return pool.make<LargeScreen>(composer, pool, columns, rows, colors, saveLines);
}

Screen* Screen::create(Composer& composer, ObjPool& pool, ResizeState& state, u16 columns, u16 rows, const TerminalColors* colors, bool reflow, Cursor* cursor) {
    if (smallScreenGeometry(columns, rows, state.saveLines)) {
        return makeScreenFromState<SmallScreen>(composer, pool, state, columns, rows, colors, reflow, cursor);
    }
    return makeScreenFromState<LargeScreen>(composer, pool, state, columns, rows, colors, reflow, cursor);
}

template <typename Coord, typename Epoch>
bool ScreenImpl<Coord, Epoch>::active() const noexcept {
    return rowRing != nullptr;
}

template <typename Coord, typename Epoch>
size_t ScreenImpl<Coord, Epoch>::cellCapacity() const noexcept {
    return (size_t)(nCols) * (nRows + saveLines);
}

template <typename Coord, typename Epoch>
u16 ScreenImpl<Coord, Epoch>::getHistoryRows() const noexcept {
    return historyRows;
}

template <typename Coord, typename Epoch>
u16 ScreenImpl<Coord, Epoch>::getViewOffset() const noexcept {
    return viewOffset;
}

template <typename Coord, typename Epoch>
u16 ScreenImpl<Coord, Epoch>::columns() const noexcept {
    return nCols;
}

template <typename Coord, typename Epoch>
u16 ScreenImpl<Coord, Epoch>::rows() const noexcept {
    return nRows;
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::expose() {
    damage.expose();
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::resetDamage() {
    damage.reset();
}

template <typename Coord, typename Epoch>
bool ScreenImpl<Coord, Epoch>::hasDamage() const noexcept {
    return damage.hasDamage();
}

template <typename Coord, typename Epoch>
Color ScreenImpl<Coord, Epoch>::getSelectionForeground() const noexcept {
    return selectionForeground;
}

template <typename Coord, typename Epoch>
Color ScreenImpl<Coord, Epoch>::getSelectionBackground() const noexcept {
    return selectionBackground;
}

template <typename Coord, typename Epoch>
u8 ScreenImpl<Coord, Epoch>::getSelectionColorMask() const noexcept {
    return selectionColorMask;
}

template <typename Coord, typename Epoch>
bool ScreenImpl<Coord, Epoch>::getBlinkVisible() const noexcept {
    return blinkVisible;
}

template <typename Coord, typename Epoch>
bool ScreenImpl<Coord, Epoch>::getCursorBlink() const noexcept {
    return cursorBlink;
}

template <typename Coord, typename Epoch>
bool ScreenImpl<Coord, Epoch>::getScreenReverseVideo() const noexcept {
    return screenReverseVideo;
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::setSelectSnapTo(SelectSnapTo value) {
    snapTo = value;
}

template <typename Coord, typename Epoch>
Screen::SelectSnapTo ScreenImpl<Coord, Epoch>::nextSelectSnapTo(SelectSnapTo value) {
    return (SelectSnapTo)(((u8)(value) + 1) % (u8)(SelectSnapTo::COUNT));
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::cycleSelectSnapTo() {
    snapTo = nextSelectSnapTo(snapTo);
}

template <typename Coord, typename Epoch>
Rect& ScreenImpl<Coord, Epoch>::getSelection() {
    return selection;
}

template <typename Coord, typename Epoch>
const Rect& ScreenImpl<Coord, Epoch>::getSelection() const {
    return selection;
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::setBlinkState(bool visible, bool cursor) {
    blinkVisible = visible;
    cursorBlink = cursor;
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::setScreenReverseVideo(bool enabled) {
    screenReverseVideo = enabled;
    expose();
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::setSelectionColor(bool foreground, Color color, bool enabled) {
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

template <typename Coord, typename Epoch>
CellExtraStore& ScreenImpl<Coord, Epoch>::cellExtras() const noexcept {
    return *composer.cellExtras;
}

template <typename Coord, typename Epoch>
ScreenImpl<Coord, Epoch>::ScreenImpl(Composer& composer_, ObjPool& pool_, u16 nCols_, u16 nRows_, const TerminalColors* colors_, u16 saveLines_)
    : nCols((Coord)(nCols_))
    , nRows((Coord)(nRows_))
    , saveLines(saveLines_)
    , viewOffset(0)
    , colors(colors_)
    , composer(composer_)
    , pool(pool_)
{
    initializeRows(nCols_, nRows_, 0);
    resizeDamage(nCols, nRows);
}

template <typename Coord, typename Epoch>
u32 ScreenImpl<Coord, Epoch>::wrapRow(i64 row) const noexcept {
    i64 result = row % rowCapacity;
    if (result < 0) {
        result += rowCapacity;
    }
    return (u32)(result);
}

template <typename Coord, typename Epoch>
TerminalCell*& ScreenImpl<Coord, Epoch>::logicalRowSlot(int row) {
    return rowRing[wrapRow((i64)(rowEnd)-nRows + row)];
}

template <typename Coord, typename Epoch>
TerminalCell* ScreenImpl<Coord, Epoch>::rawLogicalRow(int row) const noexcept {
    return rowRing[wrapRow((i64)(rowEnd)-nRows + row)];
}

template <typename Coord, typename Epoch>
TerminalCell* ScreenImpl<Coord, Epoch>::allocateRow() {
    TerminalCell* result;
    if (freeRows.empty()) {
        result = static_cast<TerminalCell*>(pool.allocate((size_t)(nCols)*cellSize));
    } else {
        result = freeRows.popBack();
    }
    memset(result, 0, (size_t)(nCols)*cellSize);
    return result;
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::releaseRow(TerminalCell* row) {
    if (row != nullptr) {
        freeRows.pushBack(row);
    }
}

template <typename Coord, typename Epoch>
TerminalCell* ScreenImpl<Coord, Epoch>::mutableLogicalRow(int row) {
    TerminalCell*& result = logicalRowSlot(row);
    if (result == nullptr) {
        result = allocateRow();
    }
    return result;
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::initializeRows(u16 columns, u16 rows, u16 history) {
    nCols = (Coord)(columns);
    nRows = (Coord)(rows);
    rowCapacity = (u32)(rows) + saveLines;
    rowRing = static_cast<TerminalCell**>(pool.allocate((size_t)(rowCapacity) * sizeof(TerminalCell*)));
    memset(rowRing, 0, (size_t)(rowCapacity) * sizeof(TerminalCell*));
    zeroRow = static_cast<TerminalCell*>(pool.allocate((size_t)(columns)*cellSize));
    memset(zeroRow, 0, (size_t)(columns)*cellSize);
    historyRows = history;
    rowEnd = ((u32)(history) + rows) % rowCapacity;
}

template <typename Coord, typename Epoch>
bool ScreenImpl<Coord, Epoch>::emptyRow(const TerminalCell* row, u16 columns) {
    for (u16 column = 0; column < columns; ++column) {
        if (row[column] != TerminalCell{}) {
            return false;
        }
    }
    return true;
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::installRow(int row, const TerminalCell* source, u16 sourceColumns) {
    const u16 count = min<u16>(sourceColumns, nCols);
    if (emptyRow(source, count)) {
        return;
    }
    TerminalCell* const destination = allocateRow();
    memcpy(destination, source, (size_t)(count)*cellSize);
    normalizeWideRow(destination, nCols);
    if (emptyRow(destination, nCols)) {
        releaseRow(destination);
        return;
    }
    logicalRowSlot(row) = destination;
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::dropScrollbackHistory() {
    viewOffset = 0;
    if (!selection.null() && selection.tl.y < 0) {
        selection.clear();
    }
    for (int row = -(int)(historyRows); row < 0; ++row) {
        TerminalCell*& slot = logicalRowSlot(row);
        releaseRow(slot);
        slot = nullptr;
    }
    historyRows = 0;
    expose();
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::collectExtraCells(Vector<TerminalCell*>& cells) {
    const auto collectRow = [&](TerminalCell* first) {
        if (first == nullptr) {
            return;
        }

        STD_ASSERT((size_t)(first) % cellSize == 0);
        u8* const begin = reinterpret_cast<u8*>(first);
        u8* const end = begin + (size_t)(nCols)*cellSize;
        u8* cursor = begin;
        while (cursor != end) {
            auto* hit = static_cast<u8*>(memchr(cursor, TerminalCell::extraRefSentinel, end - cursor));
            if (hit == nullptr) {
                break;
            }
            const size_t offset = (hit - begin) & ~(cellSize - 1);
            TerminalCell* const cell = reinterpret_cast<TerminalCell*>(begin + offset);
            if (cell->hasExtra()) {
                cells.pushBack(cell);
            }
            cursor = reinterpret_cast<u8*>(cell + 1);
        }
    };
    for (int row = -(int)(historyRows); row < nRows; ++row) {
        collectRow(rawLogicalRow(row));
    }
}

template <typename Coord, typename Epoch>
ResizeState* ScreenImpl<Coord, Epoch>::moveInto() {
    ResizeState* const state = pool.make<ResizeState>();
    state->columns = nCols;
    state->rows = nRows;
    state->saveLines = saveLines;
    state->active = active();
    state->rowRing = rowRing;
    state->zeroRow = zeroRow;
    state->rowCapacity = rowCapacity;
    state->rowEnd = rowEnd;
    state->historyRows = historyRows;
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
    rowRing = nullptr;
    zeroRow = nullptr;
    rowCapacity = 0;
    rowEnd = 0;
    historyRows = 0;
    return state;
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::layout(ResizeState& state, u16 nCols_, u16 nRows_, const TerminalColors* colors_, bool reflow, Cursor* cursorState) {
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
    lineAttrRows = 0;
    const auto scanRow = [&](const TerminalCell* first) {
        if (first == nullptr) {
            return;
        }
        lineAttrRows += first[0].line_attr != 0;
    };
    for (int row = -(int)(historyRows); row < nRows; ++row) {
        scanRow(rawLogicalRow(row));
    }
    resizeDamage(nCols, nRows);
    expose();
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::layoutCopy(ResizeState& state, u16 nCols_, u16 nRows_, Cursor* cursorState) {
    std::vector<const TerminalCell*> sourceHistory;
    sourceHistory.reserve(state.historyRows);
    for (int row = -(int)(state.historyRows); row < 0; ++row) {
        sourceHistory.push_back(stateRowPtr(state, row));
    }
    std::vector<const TerminalCell*> sourceScreen;
    sourceScreen.reserve(state.rows);
    for (u16 row = 0; row < state.rows; ++row) {
        sourceScreen.push_back(stateRowPtr(state, row));
    }
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
                    sourceHistory.erase(sourceHistory.begin());
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
                viewOffset = std::min<size_t>(viewOffset + preScroll, sourceHistory.size());
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

    viewOffset = std::min<size_t>(viewOffset, sourceHistory.size());

    // An interactive growth restores rows from history above the screen.
    std::vector<const TerminalCell*> restored;
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

    const u16 historyCount = (u16)(sourceHistory.size());
    initializeRows(nCols_, nRows_, historyCount);

    u16 outRow = 0;
    for (const TerminalCell* row : restored) {
        installRow(outRow++, row, state.columns);
    }
    for (size_t k = visibleStart; k < sourceScreen.size() && outRow < nRows_; ++k) {
        installRow(outRow++, sourceScreen[k], state.columns);
    }
    for (u16 k = 0; k < historyCount; ++k) {
        installRow((int)(k)-historyCount, sourceHistory[k], state.columns);
    }
    if (!selectionValid()) {
        selection.clear();
    }
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::layoutReflow(ResizeState& state, u16 nCols_, u16 nRows_, Cursor* cursorState) {
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

    const int oldHistoryCount = state.historyRows;
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
    Anchor viewAnchor{oldHistoryCount - (int)(viewOffset), 0};
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

    initializeRows(nCols_, nRows_, historyCount);
    for (size_t row = 0; row < nRows_; ++row) {
        installRow(row, output[screenStart + row].data(), nCols_);
    }
    for (size_t row = 0; row < historyCount; ++row) {
        installRow((int)(row)-historyCount, output[retainedStart + row].data(), nCols_);
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

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::fullCopyCells(RenderCell* const dst) const {
    CellExtraStore& extras = cellExtras();
    RenderCell* p = dst;
    for (int pY = 0; pY < nRows; ++pY) {
        const TerminalCell* src = getViewRowPtr(pY);
        for (u16 pX = 0; pX < nCols; ++pX) {
            *p++ = materialize(src[pX], extras);
        }
    }
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::deltaCopyCells(RenderCell* const dst) {
    CellExtraStore& extras = cellExtras();

    if (damage.full) {
        for (u16 row = 0; row < nRows; ++row) {
            damageDeltaCopy(dst + (size_t)(row)*nCols, getViewRowPtr(row), nCols, extras);
        }
        return;
    }

    const Packed* current = damage.cells;
    const Packed* const end = current + damage.count;
    while (current != end) {
        const u32 packed = *current++;
        const u16 row = (u16)(packed >> packShift);
        const u16 column = (u16)(packed & ((1u << packShift) - 1));
        u16 count = 1;
        while (current != end && *current == packed + count) {
            ++current;
            ++count;
        }
        const size_t offset = (size_t)(row)*nCols + column;
        damageDeltaCopy(dst + offset, getViewRowPtr(row) + column, count, extras);
    }
}

template <typename Coord, typename Epoch>
Rect ScreenImpl<Coord, Epoch>::getSelectionForView() const {
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

template <typename Coord, typename Epoch>
Rect ScreenImpl<Coord, Epoch>::getSnappedSelection() const {
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

template <typename Coord, typename Epoch>
bool ScreenImpl<Coord, Epoch>::getSelectedUtf8(std::string& utf8_selection) const {
    Rect sel = getSnappedSelection();

    if (sel.empty()) {
        return false;
    }
    sel.tl.y -= viewOffset;
    sel.br.y -= viewOffset;

    using unicodeString = std::vector<u32>;
    std::vector<unicodeString> lines;
    CellExtraStore& extras = cellExtras();
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
                const auto grapheme = extras.grapheme(cell);
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

template <typename Coord, typename Epoch>
RenderCell ScreenImpl<Coord, Epoch>::materialize(const TerminalCell& cell, const CellExtraStore& extras) const {
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

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::damageDeltaCopy(RenderCell* dst, const TerminalCell* src, u16 count, const CellExtraStore& extras) const {
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

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::setCursorPos(u16 pY, u16 pX) {
    cursor.posY = pY;
    cursor.posX = pX;
}

template <typename Coord, typename Epoch>
TerminalCursor ScreenImpl<Coord, Epoch>::getCursor() const {
    TerminalCursor ret = cursor;
    ret.posY += viewOffset;
    return ret;
}

template <typename Coord, typename Epoch>
Point ScreenImpl<Coord, Epoch>::getLogicalPoint(Point point) const {
    point.y -= viewOffset;
    return point;
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::setCursorStyle(TerminalCursor::Style cs) {
    cursor.style = cs;
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::setCursorColor(Color color) {
    cursor.color = color;
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::pageUp(u16 count) {
    u16 viewOffset_ = std::min<u32>(viewOffset + count, historyRows);
    viewOffset = viewOffset_;
    expose();
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::pageDown(u16 count) {
    viewOffset = viewOffset > count ? viewOffset - count : 0;
    expose();
}

template <typename Coord, typename Epoch>
bool ScreenImpl<Coord, Epoch>::pageToBottom() {
    if (!viewOffset) {
        return false;
    }

    viewOffset = 0;
    expose();
    return true;
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::scrollUp(u16 top, u16 bottom, u16 count) {
    count = std::min<u16>(count, bottom - top);
    const bool capture = top == 0 && saveLines != 0;
    const u16 previousViewOffset = viewOffset;
    if (!capture) {
        vscrollSelection(top, bottom, -count, false);
    }

    for (u16 k = 0; k < count; ++k) {
        if (capture) {
            TerminalCell* incoming = nullptr;
            if (historyRows == saveLines) {
                TerminalCell*& oldest = logicalRowSlot(-(int)(historyRows));
                incoming = oldest;
                oldest = nullptr;
            } else {
                ++historyRows;
            }
            rowEnd = (rowEnd + 1) % rowCapacity;
            TerminalCell*& last = logicalRowSlot(nRows - 1);
            STD_ASSERT(last == nullptr);
            last = incoming;
            for (u16 row = nRows - 1; row >= bottom; --row) {
                logicalRowSlot(row) = logicalRowSlot(row - 1);
            }
            logicalRowSlot(bottom - 1) = incoming;
        } else {
            TerminalCell* const incoming = logicalRowSlot(top);
            for (u16 row = top; row + 1 < bottom; ++row) {
                logicalRowSlot(row) = logicalRowSlot(row + 1);
            }
            logicalRowSlot(bottom - 1) = incoming;
        }
    }

    if (capture) {
        vscrollSelection(top, bottom, -count, true);
    }

    if (capture && viewOffset) {
        viewOffset = std::min<u32>(viewOffset + count, historyRows);
    }
    if (capture && previousViewOffset) {
        expose();
    } else {
        damageRectangle(top, 0, bottom, nCols);
    }
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::scrollDown(u16 top, u16 bottom, u16 count) {
    count = std::min<u16>(count, bottom - top);
    vscrollSelection(top, bottom, count, false);

    for (u16 k = 0; k < count; ++k) {
        TerminalCell* const incoming = logicalRowSlot(bottom - 1);
        for (u16 row = bottom - 1; row > top; --row) {
            logicalRowSlot(row) = logicalRowSlot(row - 1);
        }
        logicalRowSlot(top) = incoming;
    }

    damageRectangle(top, 0, bottom, nCols);
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::restoreHistory(u16 count) {
    count = std::min<u32>(count, historyRows);
    for (u16 k = 0; k < count; ++k) {
        TerminalCell*& bottom = logicalRowSlot(nRows - 1);
        TerminalCell* const outgoing = bottom;
        bottom = nullptr;
        rowEnd = wrapRow((i64)(rowEnd)-1);
        --historyRows;
        releaseRow(outgoing);
    }
    viewOffset = viewOffset > count ? viewOffset - count : 0;
    if (!selection.null()) {
        selection.tl.y += count;
        selection.br.y += count;
    }
    expose();
}

template <typename Coord, typename Epoch>
TerminalCell* ScreenImpl<Coord, Epoch>::dirtySpan(u16 pY, u16 startX, u16 count) {
    damageRow(pY, startX, startX + count);
    if (!selection.empty()) {
        invalidateSelection(Rect(startX, pY, startX + count, pY));
    }
    return mutableLogicalRow(pY) + startX;
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::fillCells(u16 ch, const TerminalCell& attrs) {
    TerminalCell fill = attrs;
    fill.uc_pt = ch == ' ' ? 0 : ch;
    fill.drawn = ch != ' ';
    for (u16 r = 0; r < nRows; ++r) {
        TerminalCell*& slot = logicalRowSlot(r);
        const bool oldLineAttribute = getLogicalRowPtr(r)[0].line_attr != 0;
        if (oldLineAttribute != (fill.line_attr != 0)) {
            lineAttrRows += attrs.line_attr != 0 ? 1 : -1;
        }
        if (fill == TerminalCell{}) {
            releaseRow(slot);
            slot = nullptr;
            continue;
        }
        TerminalCell* const row = mutableLogicalRow(r);
        for (u16 column = 0; column < nCols; ++column) {
            row[column] = fill;
        }
    }
    damageRectangle(0, 0, nRows, nCols);
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::setLineAttribute(u16 row, u8 attribute) {
    TerminalCell* cells_ = dirtySpan(row, 0, nCols);
    if ((cells_[0].line_attr != 0) != (attribute != 0)) {
        lineAttrRows += attribute != 0 ? 1 : -1;
    }
    for (u16 column = 0; column < nCols; ++column) {
        cells_[column].line_attr = attribute;
    }
}

template <typename Coord, typename Epoch>
u8 ScreenImpl<Coord, Epoch>::lineAttribute(u16 row) const noexcept {
    if (lineAttrRows == 0) {
        return 0;
    }
    return getLogicalRowPtr(row)[0].line_attr;
}

template <typename Coord, typename Epoch>
bool ScreenImpl<Coord, Epoch>::hasProtection(u16 row, u8 mask) const noexcept {
    const TerminalCell* cells_ = getLogicalRowPtr(row);
    for (u16 column = 0; column < nCols; ++column) {
        if ((cells_[column].protected_char & mask) != 0) {
            return true;
        }
    }
    return false;
}

template <typename Coord, typename Epoch>
bool ScreenImpl<Coord, Epoch>::wrapped(u16 row, u16 column) const noexcept {
    return getLogicalRowPtr(row)[column].wrap;
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::setWrapped(u16 row, u16 column) {
    TerminalCell* cells_ = mutableLogicalRow(row);
    cells_[column].wrap = 1;
    damageCell(row, column);
    if (!selection.empty()) {
        invalidateSelection(Rect(column, row));
    }
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::moveWrap(u16 row, u16 sourceColumn, u16 destinationColumn) {
    const TerminalCell* cells_ = getLogicalRowPtr(row);
    if (!cells_[sourceColumn].wrap || sourceColumn == destinationColumn) {
        return;
    }
    TerminalCell* mutableCells = mutableLogicalRow(row);
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

template <typename Coord, typename Epoch>
TerminalCell* ScreenImpl<Coord, Epoch>::prepareSpan(u16 row, u16 start, u16 count, const TerminalCell& eraseAttrs) {
    const u16 end = start + count;
    const TerminalCell* cells_ = getLogicalRowPtr(row);
    const bool splitLeft = start > 0 && (cells_[start - 1].dwidth || cells_[start].dwidth_cont);
    const bool splitRight = end < nCols && (cells_[end - 1].dwidth || cells_[end].dwidth_cont);
    if (!splitLeft && !splitRight) {
        return dirtySpan(row, start, count);
    }
    return overwriteWideSpan(row, start, count, eraseAttrs);
}

template <typename Coord, typename Epoch>
TerminalCell& ScreenImpl<Coord, Epoch>::prepareCell(u16 row, u16 column, const TerminalCell& eraseAttrs) {
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
    return mutableLogicalRow(row)[column];
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::writeGrapheme(u16 row, u16 column, const u32* codepoints, size_t count, bool wide, const TerminalCell& attrs, u32 hyperlink, u32 semantic, u8 lineAttribute_, const TerminalCell& eraseAttrs) {
    STD_ASSERT(count != 0);
    TerminalCell lead = attrs;
    lead.uc_pt = codepoints[0];
    lead.drawn = 1;
    lead.dwidth = wide;
    lead.semantic = semantic;
    lead.line_attr = lineAttribute_;
    CellExtraStore& extras = cellExtras();
    if (hyperlink != 0 || lead.hasExtra()) {
        extras.setHyperlink(lead, hyperlink);
    }
    if (count > 1) {
        extras.setGrapheme(lead, codepoints, count);
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
    if (lead.extraRef() != 0 || continuation.hasExtra()) {
        extras.setHyperlink(continuation, lead.extraRef());
    }
    prepareCell(row, column + 1, eraseAttrs) = continuation;
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::writeAsciiRun(u16 row, u16 column, const u8* input, u16 count, const TerminalCell& attrs, u32 hyperlink, u32 semantic, u8 lineAttribute_, const TerminalCell& eraseAttrs) {
    TerminalCell linkedAttrs = attrs;
    if (hyperlink != 0 || linkedAttrs.hasExtra()) {
        cellExtras().setHyperlink(linkedAttrs, hyperlink);
    }
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

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::writeRun(u16 row, u16 column, const u32* codepoints, u16 count, const TerminalCell& attrs, u32 hyperlink, u32 semantic, u8 lineAttribute_, const TerminalCell& eraseAttrs) {
    TerminalCell linkedAttrs = attrs;
    if (hyperlink != 0 || linkedAttrs.hasExtra()) {
        cellExtras().setHyperlink(linkedAttrs, hyperlink);
    }
    TerminalCell* cells_ = prepareSpan(row, column, count, eraseAttrs);
    for (u16 index = 0; index < count; ++index) {
        TerminalCell& cell = cells_[index];
        cell = linkedAttrs;
        cell.uc_pt = codepoints[index];
        cell.drawn = 1;
        cell.semantic = semantic;
        cell.line_attr = lineAttribute_;
    }
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::fillRectangle(u16 top, u16 left, u16 bottom, u16 right, u32 codepoint, const TerminalCell& attrs, const TerminalCell& eraseAttrs) {
    for (u16 row = top; row < bottom; ++row) {
        clearWideBoundary(row, left, eraseAttrs);
        clearWideBoundary(row, right, eraseAttrs);
        TerminalCell* cells_ = mutableLogicalRow(row) + left;
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

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::copyRectangle(u16 sourceTop, u16 sourceLeft, u16 targetTop, u16 targetLeft, u16 height, u16 width, const TerminalCell& eraseAttrs) {
    std::vector<TerminalCell> copied;
    copied.reserve((size_t)(height)*width);
    for (u16 row = 0; row < height; ++row) {
        const TerminalCell* source = getLogicalRowPtr(sourceTop + row) + sourceLeft;
        copied.insert(copied.end(), source, source + width);
    }
    for (u16 row = 0; row < height; ++row) {
        clearWideBoundary(targetTop + row, targetLeft, eraseAttrs);
        clearWideBoundary(targetTop + row, targetLeft + width, eraseAttrs);
        TerminalCell* destination = mutableLogicalRow(targetTop + row) + targetLeft;
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

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::changeRectangleAttributes(u16 top, u16 left, u16 bottom, u16 right, const u32* modes, size_t modeCount, bool reverse) {
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
        TerminalCell* cells_ = mutableLogicalRow(row) + left;
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

template <typename Coord, typename Epoch>
u16 ScreenImpl<Coord, Epoch>::checksum(u16 top, u16 left, u16 bottom, u16 right) const noexcept {
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

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::appendPrintableLine(u16 row, std::string& output) const {
    std::vector<u32> codepoints;
    const TerminalCell* cells_ = getLogicalRowPtr(row);
    CellExtraStore& extras = cellExtras();
    for (u16 column = 0; column < nCols; ++column) {
        const TerminalCell& cell = cells_[column];
        if (cell.dwidth_cont) {
            continue;
        }
        const GraphemeView grapheme = extras.grapheme(cell);
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

template <typename Coord, typename Epoch>
ScreenHyperlink ScreenImpl<Coord, Epoch>::hyperlinkAt(u16 row, u16 column) const {
    static constexpr size_t scanLimit = 4096;
    if (row >= nRows || column >= nCols) {
        return {};
    }
    CellExtraStore& extras = cellExtras();

    LinkPosition pointed{
        .row = (i32)(row) - (i32)(viewOffset),
        .column = column,
    };
    const TerminalCell* pointedRow = getLogicalRowPtr(pointed.row);
    if (pointedRow[0].line_attr != 0) {
        pointed.column /= 2;
    }
    if (pointedRow[pointed.column].dwidth_cont && pointed.column != 0) {
        --pointed.column;
    }

    const TerminalCell& pointedCell = getLogicalRowPtr(pointed.row)[pointed.column];
    const u32 explicitId = extras.hyperlinkDisplayId(pointedCell);
    if (explicitId != 0) {
        return {
            .payload = extras.hyperlink(pointedCell),
            .displayId = explicitId,
        };
    }

    const auto boundaryCodepoint = [](u32 codepoint) {
        if (codepoint == '"' || codepoint == '\'' || codepoint == '`' || codepoint == '<' || codepoint == '>') {
            return true;
        }
        switch (utf8proc_category(codepoint)) {
            case UTF8PROC_CATEGORY_CC:
            case UTF8PROC_CATEGORY_ZS:
            case UTF8PROC_CATEGORY_ZL:
            case UTF8PROC_CATEGORY_ZP:
                return true;
            default:
                return false;
        }
    };
    const auto boundary = [&](LinkPosition position) {
        const TerminalCell& cell = getLogicalRowPtr(position.row)[position.column];
        if (cell.conceal || extras.hyperlinkDisplayId(cell) != 0) {
            return true;
        }
        const GraphemeView grapheme = extras.grapheme(cell);
        if (grapheme.empty()) {
            return boundaryCodepoint(cell.uc_pt == 0 ? ' ' : cell.uc_pt);
        }
        for (const u32 codepoint : grapheme) {
            if (boundaryCodepoint(codepoint)) {
                return true;
            }
        }
        return false;
    };
    if (boundary(pointed)) {
        return {};
    }

    const auto validColumns = [&](i32 logicalRow) {
        return getLogicalRowPtr(logicalRow)[0].line_attr == 0 ? (u16)(nCols) : (u16)(max<Coord>((Coord)(1), nCols / 2));
    };
    const auto previous = [&](LinkPosition& position) {
        if (position.column != 0) {
            --position.column;
            const TerminalCell* cells_ = getLogicalRowPtr(position.row);
            if (cells_[position.column].dwidth_cont && position.column != 0) {
                --position.column;
            }
            return true;
        }
        const i32 minimumRow = -(i32)(historyRows);
        if (position.row <= minimumRow) {
            return false;
        }
        const i32 previousRow = position.row - 1;
        const TerminalCell* cells_ = getLogicalRowPtr(previousRow);
        const u16 columns_ = validColumns(previousRow);
        for (u16 previousColumn = columns_; previousColumn != 0; --previousColumn) {
            if (cells_[previousColumn - 1].wrap) {
                position = {
                    .row = previousRow,
                    .column = (u16)(previousColumn - 1),
                };
                if (cells_[position.column].dwidth_cont && position.column != 0) {
                    --position.column;
                }
                return true;
            }
        }
        return false;
    };
    const auto next = [&](LinkPosition& position) {
        const TerminalCell* cells_ = getLogicalRowPtr(position.row);
        const TerminalCell& cell = cells_[position.column];
        if (cell.wrap) {
            if (position.row + 1 >= nRows) {
                return false;
            }
            position = {
                .row = position.row + 1,
                .column = 0,
            };
            return true;
        }
        const u16 nextColumn = position.column + (cell.dwidth ? 2 : 1);
        if (nextColumn >= validColumns(position.row)) {
            return false;
        }
        position.column = nextColumn;
        return true;
    };

    linkLeft.clear();
    linkParts.clear();
    linkScratch.reset();

    LinkPosition position = pointed;
    size_t count = 1;
    while (previous(position)) {
        if (boundary(position)) {
            break;
        }
        if (count == scanLimit) {
            return {};
        }
        linkLeft.pushBack(position);
        ++count;
    }

    const auto append = [&](LinkPosition source) {
        LinkPart part{
            .position = source,
            .begin = linkScratch.used(),
        };
        const TerminalCell& cell = getLogicalRowPtr(source.row)[source.column];
        const GraphemeView grapheme = extras.grapheme(cell);
        const auto sink = [&](u8 byte) {
            linkScratch.append(&byte, 1);
        };
        if (grapheme.empty()) {
            Utf8Encoder::pushUnicode(cell.uc_pt == 0 ? ' ' : cell.uc_pt, sink);
        } else {
            for (const u32 codepoint : grapheme) {
                Utf8Encoder::pushUnicode(codepoint, sink);
            }
        }
        part.end = linkScratch.used();
        linkParts.pushBack(part);
    };

    for (size_t index = linkLeft.length(); index != 0; --index) {
        append(linkLeft[index - 1]);
    }
    const size_t pointedPart = linkParts.length();
    append(pointed);

    position = pointed;
    while (next(position)) {
        if (boundary(position)) {
            break;
        }
        if (count == scanLimit) {
            return {};
        }
        append(position);
        ++count;
    }

    const auto* bytes = (const u8*)(linkScratch.data());
    size_t begin = 0;
    size_t end = linkScratch.used();
    const auto asciiAlpha = [](u8 byte) {
        return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z');
    };
    const auto schemeByte = [&](u8 byte) {
        return asciiAlpha(byte) || (byte >= '0' && byte <= '9') || byte == '+' || byte == '-' || byte == '.';
    };
    size_t schemeEnd = begin;
    while (schemeEnd < end && bytes[schemeEnd] != ':') {
        ++schemeEnd;
    }
    if (schemeEnd == end) {
        return {};
    }
    begin = schemeEnd;
    while (begin != 0 && schemeByte(bytes[begin - 1])) {
        --begin;
    }
    if (schemeEnd == begin || schemeEnd - begin > 64 || !asciiAlpha(bytes[begin])) {
        return {};
    }
    for (size_t index = begin + 1; index < schemeEnd; ++index) {
        if (!schemeByte(bytes[index])) {
            return {};
        }
    }
    const auto unmatchedClosing = [&](u8 opening, u8 closing) {
        size_t openings = 0;
        size_t closings = 0;
        for (size_t index = begin; index < end; ++index) {
            openings += bytes[index] == opening;
            closings += bytes[index] == closing;
        }
        return closings > openings;
    };
    bool trimmed = true;
    while (begin < end && trimmed) {
        trimmed = false;
        const u8 last = bytes[end - 1];
        if (last == '.' || last == ',' || last == ';' || last == ':' || last == '!' || last == '?') {
            --end;
            trimmed = true;
        } else if (last == ')' && unmatchedClosing('(', ')')) {
            --end;
            trimmed = true;
        } else if (last == ']' && unmatchedClosing('[', ']')) {
            --end;
            trimmed = true;
        } else if (last == '}' && unmatchedClosing('{', '}')) {
            --end;
            trimmed = true;
        }
    }
    if (begin == end) {
        return {};
    }
    if (schemeEnd + 1 >= end) {
        return {};
    }

    const LinkPart& pointedRecord = linkParts[pointedPart];
    if (pointedRecord.end <= begin || pointedRecord.begin >= end) {
        return {};
    }

    u32 rangeBegin = (u32)(nCols)*nRows;
    u32 rangeEnd = 0;
    for (const LinkPart& part : linkParts) {
        if (part.end <= begin || part.begin >= end) {
            continue;
        }
        const i32 visibleRow = part.position.row + (i32)(viewOffset);
        if (visibleRow < 0 || visibleRow >= nRows) {
            continue;
        }
        const TerminalCell& cell = getLogicalRowPtr(part.position.row)[part.position.column];
        const u32 cellBegin = (u32)(visibleRow)*nCols + part.position.column;
        const u32 cellEnd = cellBegin + (cell.dwidth ? 2 : 1);
        rangeBegin = min(rangeBegin, cellBegin);
        rangeEnd = max(rangeEnd, min<u32>((u32)(nCols)*nRows, cellEnd));
    }
    if (rangeBegin >= rangeEnd) {
        return {};
    }
    return {
        .payload = StringView(bytes + begin, end - begin),
        .scheme = StringView(bytes + begin, schemeEnd - begin),
        .begin = rangeBegin,
        .end = rangeEnd,
    };
}

template <typename Coord, typename Epoch>
TerminalCell ScreenImpl<Coord, Epoch>::testCell(u16 row, u16 column) const noexcept {
    return getViewRowPtr(row)[column];
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::eraseInRow(u16 pY, u16 startX, u16 count, const TerminalCell& attrs) {
    if (!count) {
        return;
    }

    TerminalCell erased = attrs;
    erased.line_attr = getLogicalRowPtr(pY)[0].line_attr;
    if (startX == 0 && count == nCols) {
        if (erased == TerminalCell{}) {
            TerminalCell*& row = logicalRowSlot(pY);
            releaseRow(row);
            row = nullptr;
        } else {
            if (!erasedRowTemplateValid || erasedRowTemplate.size() != nCols || erasedRowCell != erased) {
                erasedRowTemplate.assign(nCols, erased);
                erasedRowCell = erased;
                erasedRowTemplateValid = true;
            }
            memcpy(mutableLogicalRow(pY), erasedRowTemplate.data(), nCols * cellSize);
        }
    } else if (rawLogicalRow(pY) != nullptr || erased != TerminalCell{}) {
        TerminalCell* const start = mutableLogicalRow(pY) + startX;
        eraseRange(start, start + count, erased);
    }
    damageRow(pY, startX, startX + count);
    if (!selection.empty()) {
        invalidateSelection(Rect(startX, pY, startX + count, pY));
    }
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::eraseWideInRow(u16 pY, u16 startX, u16 count, const TerminalCell& attrs) {
    if (!count) {
        return;
    }
    const u16 endX = startX + count;
    const TerminalCell* source = getLogicalRowPtr(pY);
    TerminalCell erased = attrs;
    erased.line_attr = source[0].line_attr;
    const bool eraseLeft = startX > 0 && (source[startX - 1].dwidth || source[startX].dwidth_cont);
    const bool eraseRight = endX < nCols && (source[endX - 1].dwidth || source[endX].dwidth_cont);
    if (!eraseLeft && !eraseRight) {
        eraseInRow(pY, startX, count, attrs);
        return;
    }
    TerminalCell* row = mutableLogicalRow(pY);
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

template <typename Coord, typename Epoch>
TerminalCell* ScreenImpl<Coord, Epoch>::overwriteWideSpan(u16 pY, u16 startX, u16 count, const TerminalCell& eraseAttrs) {
    const u16 endX = startX + count;
    TerminalCell* row = mutableLogicalRow(pY);
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

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::clearWideBoundary(u16 pY, u16 boundary, const TerminalCell& attrs) {
    const TerminalCell* source = getLogicalRowPtr(pY);
    const bool eraseLeft = boundary > 0 && source[boundary - 1].dwidth;
    const bool eraseRight = boundary < nCols && source[boundary].dwidth_cont;
    if (!eraseLeft && !eraseRight) {
        return;
    }
    TerminalCell* row = mutableLogicalRow(pY);
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

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::repairWideBoundary(u16 pY, u16 boundary, const TerminalCell& attrs) {
    const TerminalCell* source = getLogicalRowPtr(pY);
    const bool leftLead = boundary > 0 && source[boundary - 1].dwidth;
    const bool rightContinuation = boundary < nCols && source[boundary].dwidth_cont;
    if (leftLead == rightContinuation) {
        return;
    }
    TerminalCell* row = mutableLogicalRow(pY);
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

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::selectiveEraseInRow(u16 pY, u16 startX, u16 count, const TerminalCell& attrs, u8 protectionMask) {
    CellExtraStore& extras = cellExtras();
    TerminalCell erased = attrs;
    erased.uc_pt = 0;
    erased.protected_char = 0;
    extras.clearExtra(erased, extras.underlineColor(attrs));
    if (rawLogicalRow(pY) == nullptr && erased == TerminalCell{}) {
        damageRow(pY, startX, startX + count);
        if (!selection.empty()) {
            invalidateSelection(Rect(startX, pY, startX + count, pY));
        }
        return;
    }
    TerminalCell* row = mutableLogicalRow(pY);
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

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::moveInRow(u16 pY, u16 dstX, u16 srcX, u16 count) {
    if (!count) {
        return;
    }

    TerminalCell* const row = rawLogicalRow(pY);
    if (row != nullptr) {
        moveCells(row + dstX, row + srcX, count);
    }
    damageRow(pY, dstX, dstX + count);
    if (!selection.empty()) {
        invalidateSelection(Rect(dstX, pY, dstX + count, pY));
    }
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::copyRow(u16 dstY, u16 srcY, u16 startX, u16 count) {
    if (!count) {
        return;
    }

    const TerminalCell* const source = getLogicalRowPtr(srcY) + startX;
    TerminalCell* destinationRow = rawLogicalRow(dstY);
    if (destinationRow == nullptr && emptyRow(source, count)) {
        damageRow(dstY, startX, startX + count);
        if (!selection.empty()) {
            invalidateSelection(Rect(startX, dstY, startX + count, dstY));
        }
        return;
    }
    destinationRow = mutableLogicalRow(dstY);
    TerminalCell* const destination = destinationRow + startX;
    if (startX == 0 && (destination[0].line_attr != 0) != (source[0].line_attr != 0)) {
        lineAttrRows += source[0].line_attr != 0 ? 1 : -1;
    }
    copyCells(destination, source, count);
    if (startX == 0 && count == nCols && emptyRow(destinationRow, nCols)) {
        TerminalCell*& slot = logicalRowSlot(dstY);
        releaseRow(slot);
        slot = nullptr;
    }
    damageRow(dstY, startX, startX + count);
    if (!selection.empty()) {
        invalidateSelection(Rect(startX, dstY, startX + count, dstY));
    }
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::rotateRowsUp(u16 top, u16 bottom, u16 count) {
    count = std::min<u16>(count, bottom - top);
    if (!count) {
        return;
    }
    if (!selection.empty()) {
        invalidateSelection(Rect(0, top, 0, bottom));
    }
    for (u16 rotation = 0; rotation < count; ++rotation) {
        TerminalCell* const first = logicalRowSlot(top);
        for (u16 row = top; row + 1 < bottom; ++row) {
            logicalRowSlot(row) = logicalRowSlot(row + 1);
        }
        logicalRowSlot(bottom - 1) = first;
    }
    damageRectangle(top, 0, bottom, nCols);
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::rotateRowsDown(u16 top, u16 bottom, u16 count) {
    count = std::min<u16>(count, bottom - top);
    if (!count) {
        return;
    }
    if (!selection.empty()) {
        invalidateSelection(Rect(0, top, 0, bottom));
    }
    for (u16 rotation = 0; rotation < count; ++rotation) {
        TerminalCell* const last = logicalRowSlot(bottom - 1);
        for (u16 row = bottom - 1; row > top; --row) {
            logicalRowSlot(row) = logicalRowSlot(row - 1);
        }
        logicalRowSlot(top) = last;
    }
    damageRectangle(top, 0, bottom, nCols);
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::invalidateSelection(const Rect&& damage) {
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

template <typename Coord, typename Epoch>
bool ScreenImpl<Coord, Epoch>::selectionValid() const {
    if (selection.null()) {
        return true;
    }

    const int firstRow = -(int)(historyRows);
    const auto valid = [&](Point point) {
        return point.x >= 0 && point.x <= nCols && point.y >= firstRow && point.y < nRows;
    };
    return valid(selection.tl) && valid(selection.br);
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::vscrollSelection(u16 top, u16 bottom, int vertOffset, bool captureHistory) {
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
        if (selection.tl.y < -(int)(historyRows)) {
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

template <typename Coord, typename Epoch>
const TerminalCell* ScreenImpl<Coord, Epoch>::getLogicalRowPtr(int pY) const {
    const TerminalCell* const row = rawLogicalRow(pY);
    return row != nullptr ? row : zeroRow;
}

template <typename Coord, typename Epoch>
const TerminalCell* ScreenImpl<Coord, Epoch>::getViewRowPtr(int pY) const {
    return getLogicalRowPtr(pY - viewOffset);
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::eraseRange(TerminalCell* start, TerminalCell* end, const TerminalCell& attrs) {
    while (start < end) {
        *start++ = attrs;
    }
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::copyCells(TerminalCell* destination, const TerminalCell* source, u32 count) {
    memcpy(destination, source, count * cellSize);
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::moveCells(TerminalCell* destination, const TerminalCell* source, u32 count) {
    memmove(destination, source, count * cellSize);
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::damageCell(u16 row, u16 column) {
    const u32 viewRow = (u32)(row) + viewOffset;
    if (viewRow < nRows) {
        damage.addCell(viewRow, column);
    }
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::damageRow(u16 row, u16 begin, u16 end) {
    const u32 viewRow = (u32)(row) + viewOffset;
    if (viewRow < nRows) {
        damage.addRow(viewRow, begin, end);
    }
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::damageRectangle(u16 top, u16 left, u16 bottom, u16 right) {
    const u32 viewTop = (u32)(top) + viewOffset;
    const u32 viewBottom = (u32)(bottom) + viewOffset;
    if (viewTop >= nRows) {
        return;
    }
    damage.addRect(viewTop, left, viewBottom < nRows ? viewBottom : nRows, right);
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::resizeDamage(u16 columns, u16 rows) {
    const size_t count = (size_t)(columns)*rows;
    damageStorage.grow(count * (sizeof(Epoch) + sizeof(Packed)));
    damage.configure(damageStorage.mutData(), columns, rows);
}

template <typename Coord, typename Epoch>
auto ScreenImpl<Coord, Epoch>::Damage::operator=(Damage&& other) noexcept -> Damage& {
    cells = other.cells;
    epochs = other.epochs;
    capacity = other.capacity;
    count = other.count;
    epoch = other.epoch;
    width = other.width;
    height = other.height;
    full = other.full;
    other.cells = nullptr;
    other.epochs = nullptr;
    other.capacity = 0;
    other.count = 0;
    return *this;
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::Damage::configure(void* storage, u16 columns, u16 rows_) {
    width = (Coord)(columns);
    height = (Coord)(rows_);
    const size_t required = (size_t)(width)*height;
    // The epoch array leads: its alignment requirement is not smaller than
    // the packed coordinate's in either instantiation.
    epochs = static_cast<Epoch*>(storage);
    cells = reinterpret_cast<Packed*>(epochs + required);
    capacity = required;
    if (required != 0) {
        memset(epochs, 0, required * sizeof(Epoch));
    }
    count = 0;
    epoch = 1;
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::Damage::reset() {
    count = 0;
    full = false;
    ++epoch;
    if (epoch == 0) {
        memset(epochs, 0, (size_t)(width)*height * sizeof(Epoch));
        epoch = 1;
    }
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::Damage::expose() {
    // The full flag stands in for per-cell marks: materializing a whole
    // screen of coordinates is the single most frequent damage pattern.
    full = true;
    count = 0;
}

template <typename Coord, typename Epoch>
bool ScreenImpl<Coord, Epoch>::Damage::hasDamage() const noexcept {
    return full || count != 0;
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::Damage::addCell(u16 row, u16 column) {
    if (full || count == capacity) {
        return;
    }
    const size_t index = (size_t)(row)*width + column;
    Epoch& cellEpoch = epochs[index];
    if (cellEpoch == epoch) {
        return;
    }
    cellEpoch = epoch;
    cells[count++] = (Packed)(((u32)(row) << packShift) | column);
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::Damage::addRow(u16 row, u16 begin, u16 end) {
    if (full || end <= begin || count == capacity) {
        return;
    }
    const size_t rowOffset = (size_t)(row)*width;
    Epoch* const cellEpochs = epochs + rowOffset;
    for (u16 column = begin; column < end; ++column) {
        if (cellEpochs[column] != epoch) {
            cellEpochs[column] = epoch;
            cells[count++] = (Packed)(((u32)(row) << packShift) | column);
        }
    }
}

template <typename Coord, typename Epoch>
void ScreenImpl<Coord, Epoch>::Damage::addRect(u16 top, u16 left, u16 bottom, u16 right) {
    if (full || bottom <= top || right <= left || count == capacity) {
        return;
    }
    for (u16 row = top; row < bottom; ++row) {
        addRow(row, left, right);
    }
}
