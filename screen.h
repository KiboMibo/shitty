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

struct ScreenFrame {
    TerminalCellBatch damage;
    Rect selection;
    Rect snappedSelection;
    u32 historyRows = 0;
    u32 viewOffset = 0;
};

struct ScreenInfo {
    u32 revision = 0;
    size_t cellCapacity = 0;
    u32 historyRows = 0;
    u32 viewOffset = 0;
    u16 columns = 0;
    u16 rows = 0;
};

enum ScreenChecksumFlag : u8 {
    ChecksumPositive = 1 << 0,
    ChecksumNoAttributes = 1 << 1,
    ChecksumKeepBlanks = 1 << 2,
    ChecksumIncludeUndrawn = 1 << 3,
    ChecksumRawCodepoint = 1 << 4,
};

struct Screen {
    struct Cursor {
        Point position;
        bool pendingWrap = false;
    };

    struct WriteResult {
        u16 count;
        u16 end;
    };

    enum class SelectSnapTo : u8 {
        Char = 0,
        Word,
        Line,
        COUNT
    };

    virtual Screen* resized(stl::ObjPool& pool, u16 columns, u16 rows, Cursor& cursor) = 0;
    virtual void dropHistory() = 0;
    virtual bool scrollView(i32 rows) = 0;

    virtual void fillCells(u16 ch, const TerminalCell& attrs) = 0;
    virtual void setLineAttribute(u16 row, u8 attribute) = 0;
    virtual u8 lineAttribute(u16 row) const noexcept = 0;
    virtual bool hasProtection(u16 row, u8 mask) const noexcept = 0;
    virtual bool wrapped(u16 row, u16 column) const noexcept = 0;
    virtual void setWrapped(u16 row, u16 column) = 0;
    virtual void writeGrapheme(u16 row, u16 column, const u32* codepoints, size_t count, bool wide, const TerminalCell& attrs, u32 hyperlink, u32 semantic, const TerminalCell& eraseAttrs) = 0;
    virtual WriteResult writeAsciiRun(u16 row, u16 column, u16 normalEnd, u16 doubleEnd, const u8* input, u16 count, const TerminalCell& attrs, u32 hyperlink, u32 semantic, const TerminalCell& eraseAttrs) = 0;
    virtual void writeAsciiLines(u16 row, const u8* input, const u16* lengths, u16 lineCount, const TerminalCell& attrs, u32 hyperlink, u32 semantic, const TerminalCell& eraseAttrs) = 0;
    virtual WriteResult writeAsciiRunInsert(u16 row, u16 column, u16 normalEnd, u16 doubleEnd, const u8* input, u16 count, const TerminalCell& attrs, u32 hyperlink, u32 semantic, const TerminalCell& eraseAttrs) = 0;
    virtual void writeRun(u16 row, u16 column, const u32* codepoints, u16 count, const TerminalCell& attrs, u32 hyperlink, u32 semantic, const TerminalCell& eraseAttrs) = 0;
    virtual void writeRepeatedCodepoint(u16 row, u16 column, u16 count, u32 codepoint, const TerminalCell& attrs, u32 hyperlink, u32 semantic, const TerminalCell& eraseAttrs) = 0;
    virtual void writeGlyphRun(u16 row, u16 column, const u32* codepoints, const u8* widths, u16 glyphCount, u16 cellCount, const TerminalCell& attrs, u32 hyperlink, u32 semantic, const TerminalCell& eraseAttrs) = 0;
    virtual void fillRectangle(u16 top, u16 left, u16 bottom, u16 right, u32 codepoint, const TerminalCell& attrs, const TerminalCell& eraseAttrs) = 0;
    virtual void copyRectangle(u16 sourceTop, u16 sourceLeft, u16 targetTop, u16 targetLeft, u16 height, u16 width, const TerminalCell& eraseAttrs) = 0;
    virtual void changeRectangleAttributes(u16 top, u16 left, u16 bottom, u16 right, CellAttributeChange change) = 0;
    virtual u16 checksum(u16 top, u16 left, u16 bottom, u16 right, u8 flags) const noexcept = 0;
    virtual ScreenHyperlink hyperlinkAt(u16 row, u16 column) const = 0;
    virtual TerminalCell testCell(u16 row, u16 column) const noexcept = 0;
    virtual TerminalCell testLogicalCell(i32 row, u16 column) const noexcept = 0;
    virtual u32 testMaterializedRows() const noexcept = 0;
    virtual ScreenFrame captureFrame(TerminalCellSpan* spans) const = 0;
    virtual ScreenInfo info() const noexcept = 0;

    virtual void collectExtraCells(stl::Vector<TerminalCell*>& cells) = 0;

    virtual void eraseCells(u16 row, u16 start, u16 count, const TerminalCell& attrs) = 0;
    virtual void selectiveEraseCells(u16 row, u16 start, u16 count, const TerminalCell& attrs, u8 protectionMask = 0xff) = 0;
    virtual void insertCells(u16 row, u16 start, u16 end, u16 count, const TerminalCell& attrs) = 0;
    virtual void deleteCells(u16 row, u16 start, u16 end, u16 count, const TerminalCell& attrs) = 0;
    virtual void copyRow(u16 destinationRow, u16 sourceRow, u16 start, u16 count, const TerminalCell& attrs) = 0;
    virtual void scrollRectangle(u16 top, u16 left, u16 bottom, u16 right, i32 rows, const TerminalCell& attrs) = 0;
    virtual void rotateRows(u16 top, u16 bottom, i32 rows) = 0;
    virtual void scrollRows(u16 top, u16 bottom, i32 rows, const TerminalCell& attrs) = 0;

    virtual void expose() = 0;
    virtual void resetDamage() = 0;
    virtual bool hasBlinkingText() const noexcept = 0;

    virtual Rect currentSelection() const noexcept = 0;
    virtual bool hasSelection() const = 0;
    virtual void beginSelection(Point point) = 0;
    virtual void updateSelection(Rect selection) = 0;
    virtual void cycleSelectionSnap() = 0;
    virtual void clearSelection() = 0;
    virtual bool selectedText(std::string& text) const = 0;
    virtual Point logicalPoint(Point point) const = 0;

    void writeCodepoint(u16 row, u16 column, u32 codepoint, bool wide, const TerminalCell& attrs, u32 hyperlink, u32 semantic, const TerminalCell& eraseAttrs) {
        writeGrapheme(row, column, &codepoint, 1, wide, attrs, hyperlink, semantic, eraseAttrs);
    }

    void writeAsciiRun(u16 row, u16 column, const u8* input, u16 count, const TerminalCell& attrs, u32 hyperlink, u32 semantic, const TerminalCell& eraseAttrs) {
        const u16 columns = info().columns;
        writeAsciiRun(row, column, columns, columns, input, count, attrs, hyperlink, semantic, eraseAttrs);
    }

    void writeAsciiRunInsert(u16 row, u16 column, u16 end, const u8* input, u16 count, const TerminalCell& attrs, u32 hyperlink, u32 semantic, const TerminalCell& eraseAttrs) {
        writeAsciiRunInsert(row, column, end, end, input, count, attrs, hyperlink, semantic, eraseAttrs);
    }

    static Screen* createPrimary(Composer& composer, stl::ObjPool& pool, u16 columns, u16 rows, const TerminalColors* colors, u16 saveLines);
    static Screen* createAlternate(Composer& composer, stl::ObjPool& pool, u16 columns, u16 rows, const TerminalColors* colors);
    static Screen* createInactiveAlternate(Composer& composer, stl::ObjPool& pool);
};
