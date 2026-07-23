/* This file is part of Shitty.
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

#include <std/lib/list.h>
#include <std/lib/vector.h>
#include <std/str/view.h>

namespace stl {
    class ObjPool;
}

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

struct HyperlinkHandle final: stl::IntrusiveNode {
    stl::StringView identity;
    stl::StringView payload;
    u32 displayId = 0;
    u32 identityNext = 0;

    HyperlinkHandle(stl::StringView identity_, stl::StringView payload_, u32 displayId_) noexcept
        : identity(identity_)
        , payload(payload_)
        , displayId(displayId_)
    {
    }
};

struct CellExtra {
    stl::IntrusiveList hyperlinks;
    stl::StringView graphemeBytes;
    CellColor underlineColor = CellColor::defaultForeground();
};

struct CellExtraSpec {
    CellColor underlineColor = CellColor::defaultForeground();
    GraphemeView grapheme;
    stl::StringView hyperlinkIdentity;
    stl::StringView hyperlinkPayload;
    u32 hyperlinkDisplayId = 0;

    bool hasGrapheme() const noexcept {
        return !grapheme.empty();
    }

    bool hasHyperlink() const noexcept {
        return hyperlinkDisplayId != 0;
    }

    bool needsExtra() const noexcept {
        return hasGrapheme() || hasHyperlink();
    }
};

class CellExtraStore {
public:
    explicit CellExtraStore(size_t cellCount);
    ~CellExtraStore() noexcept;

    CellExtraStore(const CellExtraStore&) = delete;
    CellExtraStore& operator=(const CellExtraStore&) = delete;

    const CellExtra* get(u32 ref) const noexcept;
    CellExtraSpec describe(u32 ref) const noexcept;

    u32 migrate(const CellExtraStore& source, u32 sourceRef);

    CellColor underlineColor(const TerminalCell& cell) const noexcept;
    GraphemeView grapheme(const TerminalCell& cell) const noexcept;
    GraphemeView grapheme(u32 ref) const noexcept;
    stl::StringView hyperlink(const TerminalCell& cell) const noexcept;
    u32 hyperlinkDisplayId(const TerminalCell& cell) const noexcept;

    u32 getOrCreateHyperlink(stl::StringView identity, stl::StringView payload, u32 displayId);
    u32 findHyperlink(stl::StringView identity) const noexcept;
    size_t hyperlinkCount() const noexcept {
        return hyperlinkCount_;
    }

    void setUnderlineColor(TerminalCell& cell, CellColor color);
    void setGrapheme(TerminalCell& cell, const u32* codepoints, size_t count);
    void clearGrapheme(TerminalCell& cell);
    void setHyperlink(TerminalCell& cell, u32 hyperlinkRef);
    void clearHyperlink(TerminalCell& cell);
    void clearExtra(TerminalCell& cell, CellColor underlineColor);

    size_t slotCount() const noexcept {
        return slots_.length();
    }

    size_t allocatedExtraBytes() const noexcept {
        return allocatedExtraBytes_;
    }

    size_t allocationsSinceGc() const noexcept {
        return allocationsSinceGc_;
    }

    void setCellCount(size_t cellCount) noexcept;
    void finishCollection() noexcept;
    bool shouldCollect() const noexcept;
    bool hardLimitExceeded() const noexcept;

private:
    stl::ObjPool* pool_;
    stl::Vector<CellExtra*> slots_;
    // OSC 8 identity lookup only. This is not a CellExtra content index:
    // append() always creates a new dense ref.
    stl::Vector<u32> hyperlinkBuckets_;
    size_t allocatedExtraBytes_ = 256;
    size_t allocationsSinceGc_ = 0;
    size_t cellCount_ = 0;
    size_t byteBudget_ = 0;
    size_t slotBudget_ = 0;
    size_t allocationBudget_ = 0;
    size_t hyperlinkCount_ = 0;

    static GraphemeView graphemeOf(const CellExtra& extra) noexcept;
    static HyperlinkHandle* hyperlinkOf(CellExtra& extra) noexcept;
    static const HyperlinkHandle* hyperlinkOf(const CellExtra& extra) noexcept;

    u32 append(const CellExtraSpec& spec);
    void apply(TerminalCell& cell, const CellExtraSpec& spec);
    void rehashHyperlinks(size_t capacity);
    stl::StringView copyBytes(stl::StringView value);
};
