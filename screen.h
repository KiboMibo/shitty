/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "rect.h"
#include "terminal_types.h"

#include <std/lib/vector.h>
#include <std/str/view.h>
#include <std/sys/types.h>

#include <cstddef>
#include <string>

struct CellExtraStore;
struct Composer;
struct ResizeState;

namespace stl {
    class ObjPool;
}

struct ScreenHyperlink {
    stl::StringView payload;
    stl::StringView scheme;
    u32 displayId = 0;
    u32 begin = 0;
    u32 end = 0;
};

struct Screen {
    struct Cursor {
        Point position;
        bool pendingWrap = false;
    };

    enum class SelectSnapTo : u8 {
        Char = 0,
        Word,
        Line,
        COUNT
    };

    // Exposes the geometry-independent screen state through an opaque handle
    // allocated from the screen's own pool; the screen itself becomes empty.
    // The handle borrows row storage, so the pool must stay alive until a
    // replacement screen has copied the retained rows.
    virtual ResizeState* moveInto() = 0;
    virtual void dropScrollbackHistory() = 0;

    virtual void fillCells(u16 ch, const TerminalCell& attrs) = 0;
    virtual void setLineAttribute(u16 row, u8 attribute) = 0;
    virtual u8 lineAttribute(u16 row) const noexcept = 0;
    virtual bool hasProtection(u16 row, u8 mask) const noexcept = 0;
    virtual bool wrapped(u16 row, u16 column) const noexcept = 0;
    virtual void setWrapped(u16 row, u16 column) = 0;
    virtual void writeCodepoint(u16 row, u16 column, u32 codepoint, bool wide, const TerminalCell& attrs, u32 hyperlink, u32 semantic, const TerminalCell& eraseAttrs) = 0;
    virtual void writeGrapheme(u16 row, u16 column, const u32* codepoints, size_t count, bool wide, const TerminalCell& attrs, u32 hyperlink, u32 semantic, const TerminalCell& eraseAttrs) = 0;
    virtual void writeAsciiRun(u16 row, u16 column, const u8* input, u16 count, const TerminalCell& attrs, u32 hyperlink, u32 semantic, const TerminalCell& eraseAttrs) = 0;
    virtual void writeAsciiLines(u16 row, const u8* input, const u16* lengths, u16 lineCount, const TerminalCell& attrs, u32 hyperlink, u32 semantic, const TerminalCell& eraseAttrs) = 0;
    virtual void writeAsciiRunInsert(u16 row, u16 column, u16 end, const u8* input, u16 count, const TerminalCell& attrs, u32 hyperlink, u32 semantic, const TerminalCell& eraseAttrs) = 0;
    virtual void writeRun(u16 row, u16 column, const u32* codepoints, u16 count, const TerminalCell& attrs, u32 hyperlink, u32 semantic, const TerminalCell& eraseAttrs) = 0;
    virtual void writeGlyphRun(u16 row, u16 column, const u32* codepoints, const u8* widths, u16 glyphCount, u16 cellCount, const TerminalCell& attrs, u32 hyperlink, u32 semantic, const TerminalCell& eraseAttrs) = 0;
    virtual void fillRectangle(u16 top, u16 left, u16 bottom, u16 right, u32 codepoint, const TerminalCell& attrs, const TerminalCell& eraseAttrs) = 0;
    virtual void copyRectangle(u16 sourceTop, u16 sourceLeft, u16 targetTop, u16 targetLeft, u16 height, u16 width, const TerminalCell& eraseAttrs) = 0;
    virtual void changeRectangleAttributes(u16 top, u16 left, u16 bottom, u16 right, const u32* modes, size_t modeCount, bool reverse) = 0;
    virtual u16 checksum(u16 top, u16 left, u16 bottom, u16 right) const noexcept = 0;
    virtual void appendPrintableLine(u16 row, std::string& output) const = 0;
    virtual ScreenHyperlink hyperlinkAt(u16 row, u16 column) const = 0;
    virtual TerminalCell testCell(u16 row, u16 column) const noexcept = 0;
    virtual TerminalCellBatch copyDamage(TerminalCellSpan* spans) const = 0;

    virtual bool active() const noexcept = 0;

    virtual CellExtraStore& cellExtras() const noexcept = 0;
    virtual void collectExtraCells(stl::Vector<TerminalCell*>& cells) = 0;
    virtual void damageExtraCells() = 0;
    virtual size_t cellCapacity() const noexcept = 0;

    virtual void eraseCells(u16 row, u16 start, u16 count, const TerminalCell& attrs) = 0;
    virtual void selectiveEraseCells(u16 row, u16 start, u16 count, const TerminalCell& attrs, u8 protectionMask = 0xff) = 0;
    virtual void insertCells(u16 row, u16 start, u16 end, u16 count, const TerminalCell& attrs) = 0;
    virtual void deleteCells(u16 row, u16 start, u16 end, u16 count, const TerminalCell& attrs) = 0;
    virtual void copyRow(u16 destinationRow, u16 sourceRow, u16 start, u16 count, const TerminalCell& attrs) = 0;
    virtual void scrollRectangleUp(u16 top, u16 left, u16 bottom, u16 right, u16 count, const TerminalCell& attrs) = 0;
    virtual void scrollRectangleDown(u16 top, u16 left, u16 bottom, u16 right, u16 count, const TerminalCell& attrs) = 0;
    virtual void rotateRowsUp(u16 top, u16 bottom, u16 count) = 0;
    virtual void rotateRowsDown(u16 top, u16 bottom, u16 count) = 0;

    virtual void scrollUp(u16 top, u16 bottom, u16 count, const TerminalCell& attrs) = 0;
    virtual void scrollDown(u16 top, u16 bottom, u16 count, const TerminalCell& attrs) = 0;
    virtual void restoreHistory(u16 count) = 0;

    virtual void pageUp(u16 count) = 0;
    virtual void pageDown(u16 count) = 0;
    virtual bool pageToBottom() = 0;

    virtual u32 getHistoryRows() const noexcept = 0;
    virtual u32 getViewOffset() const noexcept = 0;
    virtual u16 columns() const noexcept = 0;
    virtual u16 rows() const noexcept = 0;

    virtual void expose() = 0;
    virtual void resetDamage() = 0;
    virtual bool hasDamage() const noexcept = 0;

    virtual TerminalCursor getCursor() const = 0;
    virtual void setCursorPos(u16 row, u16 column) = 0;
    virtual void setCursorStyle(TerminalCursor::Style style) = 0;
    virtual void setCursorColor(Color color) = 0;
    virtual void setSelectionColor(bool foreground, Color color, bool enabled) = 0;

    virtual Color getSelectionForeground() const noexcept = 0;
    virtual Color getSelectionBackground() const noexcept = 0;
    virtual u8 getSelectionColorMask() const noexcept = 0;

    virtual void setBlinkState(bool visible, bool cursor) = 0;
    virtual bool getBlinkVisible() const noexcept = 0;
    virtual bool getCursorBlink() const noexcept = 0;

    virtual void setScreenReverseVideo(bool enabled) = 0;
    virtual bool getScreenReverseVideo() const noexcept = 0;

    virtual void setSelectSnapTo(SelectSnapTo snapTo) = 0;
    virtual void cycleSelectSnapTo() = 0;
    virtual Rect& getSelection() = 0;
    virtual const Rect& getSelection() const = 0;
    virtual Rect getSelectionForView() const = 0;
    virtual Rect getSnappedSelection() const = 0;
    virtual bool getSelectedUtf8(std::string& text) const = 0;
    virtual Point getLogicalPoint(Point point) const = 0;

    static Screen* create(Composer& composer, stl::ObjPool& pool);
    static Screen* create(Composer& composer, stl::ObjPool& pool, u16 columns, u16 rows, const TerminalColors* colors, u16 saveLines = 0);
    static Screen* create(Composer& composer, stl::ObjPool& pool, ResizeState& state, u16 columns, u16 rows, const TerminalColors* colors, bool reflow, Cursor* cursor);
};
