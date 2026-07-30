/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "terminal_types.h"

#include <std/lib/vector.h>
#include <std/str/view.h>

struct Composer;

struct GraphemeView {
    const u32* values = nullptr;
    u32 count = 0;

    const u32* begin() const noexcept {
        return values;
    }

    const u32* end() const noexcept {
        return count == 0 ? values : values + count;
    }

    const u32* data() const noexcept {
        return values;
    }

    size_t size() const noexcept {
        return count;
    }

    bool empty() const noexcept {
        return count == 0;
    }

    const u32& operator[](size_t index) const noexcept {
        return values[index];
    }
};

struct CellExtraView {
    CellColor underlineColor;
    GraphemeView grapheme;
    u32 hyperlinkDisplayId = 0;
};

struct MulticellSpec {
    u16 columns = 0;
    u16 rows = 0;
    u8 scale = 1;
    u8 width = 0;
    u8 numerator = 0;
    u8 denominator = 0;
    u8 verticalAlignment = 0;
    u8 horizontalAlignment = 0;
};

struct MulticellView {
    const void* identity = nullptr;
    GraphemeView text;
    MulticellSpec spec;
    u16 column = 0;
    u16 row = 0;

    bool valid() const noexcept {
        return identity != nullptr;
    }

    bool head() const noexcept {
        return valid() && column == 0 && row == 0;
    }
};

struct MulticellHandle;

struct CellExtraStore {
    virtual CellExtraView view(const TerminalCell& cell) const noexcept = 0;
    virtual MulticellView multicell(const TerminalCell& cell) const noexcept = 0;
    virtual CellColor underlineColor(const TerminalCell& cell) const noexcept = 0;
    virtual GraphemeView grapheme(const TerminalCell& cell) const noexcept = 0;
    virtual GraphemeView grapheme(u32 ref) const noexcept = 0;
    virtual stl::StringView hyperlink(const TerminalCell& cell) const noexcept = 0;
    virtual u32 hyperlinkDisplayId(const TerminalCell& cell) const noexcept = 0;

    virtual u32 getOrCreateHyperlink(stl::StringView identity, stl::StringView payload, u32 displayId) = 0;
    virtual u32 findHyperlink(stl::StringView identity) const noexcept = 0;
    virtual size_t hyperlinkCount() const noexcept = 0;

    virtual void setUnderlineColor(TerminalCell& cell, CellColor color) = 0;
    virtual void setGrapheme(TerminalCell& cell, const u32* codepoints, size_t count) = 0;
    virtual void clearGrapheme(TerminalCell& cell) = 0;
    virtual MulticellHandle* createMulticell(const u32* codepoints, size_t count, const MulticellSpec& spec) = 0;
    virtual void setMulticell(TerminalCell& cell, MulticellHandle* handle, u16 column, u16 row) = 0;
    virtual void setHyperlink(TerminalCell& cell, u32 hyperlinkRef) = 0;
    virtual void clearHyperlink(TerminalCell& cell) = 0;
    virtual void clearExtra(TerminalCell& cell, CellColor underlineColor) = 0;

    virtual void setCellCount(size_t cellCount) noexcept = 0;
    virtual bool shouldCollect() const noexcept = 0;
    virtual bool hardLimitExceeded() const noexcept = 0;
    virtual void collect(stl::Vector<TerminalCell*>& cells, u32* const* roots, size_t rootCount) = 0;

    static CellExtraStore* create(Composer& composer, size_t cellCount);
};
