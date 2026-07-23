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

#include "cell_extra.h"

#include <std/mem/obj_pool.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>
#include <new>

namespace stl {}
using namespace stl;

namespace {
    bool equalBytes(StringView left, StringView right) noexcept {
        return left.length() == right.length()
            && (left.empty() || memcmp(left.data(), right.data(), left.length()) == 0);
    }
}

CellExtraStore::CellExtraStore(size_t cellCount)
    : pool_(ObjPool::fromMemoryRaw())
{
    slots_.pushBack(nullptr);
    rehashHyperlinks(16);
    setCellCount(cellCount);
}

CellExtraStore::~CellExtraStore() noexcept {
    delete pool_;
}

const CellExtra* CellExtraStore::get(u32 ref) const noexcept {
    if (ref == 0 || ref >= slots_.length()) {
        return nullptr;
    }
    return slots_[ref];
}

GraphemeView CellExtraStore::graphemeOf(const CellExtra& extra) noexcept {
    return {
        reinterpret_cast<const u32*>(extra.graphemeBytes.data()),
        static_cast<u32>(extra.graphemeBytes.length() / sizeof(u32)),
    };
}

const HyperlinkHandle* CellExtraStore::hyperlinkOf(const CellExtra& extra) noexcept {
    const IntrusiveNode* node = extra.hyperlinks.front();
    return node == extra.hyperlinks.end() ? nullptr : static_cast<const HyperlinkHandle*>(node);
}

HyperlinkHandle* CellExtraStore::hyperlinkOf(CellExtra& extra) noexcept {
    IntrusiveNode* node = extra.hyperlinks.mutFront();
    return node == extra.hyperlinks.mutEnd() ? nullptr : static_cast<HyperlinkHandle*>(node);
}

CellExtraSpec CellExtraStore::describe(u32 ref) const noexcept {
    CellExtraSpec spec;
    const CellExtra* extra = get(ref);
    if (extra == nullptr) {
        return spec;
    }
    spec.underlineColor = extra->underlineColor;
    spec.grapheme = graphemeOf(*extra);
    if (const HyperlinkHandle* hyperlink = hyperlinkOf(*extra); hyperlink != nullptr) {
        spec.hyperlinkIdentity = hyperlink->identity;
        spec.hyperlinkPayload = hyperlink->payload;
        spec.hyperlinkDisplayId = hyperlink->displayId;
    }
    return spec;
}

StringView CellExtraStore::copyBytes(StringView value) {
    if (value.empty()) {
        return {};
    }
    allocatedExtraBytes_ += value.length() + 1;
    return pool_->intern(value);
}

void CellExtraStore::rehashHyperlinks(size_t capacity) {
    Vector<u32> replacement(capacity);
    replacement.zero(capacity);
    for (u32 head : hyperlinkBuckets_) {
        for (u32 ref = head; ref != 0;) {
            CellExtra* extra = slots_[ref];
            HyperlinkHandle* hyperlink = hyperlinkOf(*extra);
            const u32 next = hyperlink->identityNext;
            const size_t bucket = hyperlink->identity.hash64() % capacity;
            hyperlink->identityNext = replacement[bucket];
            replacement.mut(bucket) = ref;
            ref = next;
        }
    }
    hyperlinkBuckets_.xchg(replacement);
}

u32 CellExtraStore::append(const CellExtraSpec& spec) {
    assert(spec.needsExtra());
    if (slots_.length() >= std::numeric_limits<u32>::max()) {
        throw std::bad_alloc();
    }

    const u32 knownHyperlink = spec.hasHyperlink()
        ? findHyperlink(spec.hyperlinkIdentity) : 0;
    const bool newHyperlink = spec.hasHyperlink() && knownHyperlink == 0;
    if (newHyperlink
        && (hyperlinkCount_ + 1) * 10 >= hyperlinkBuckets_.length() * 7) {
        rehashHyperlinks(hyperlinkBuckets_.length() * 2);
    }

    // CellExtra owns only arena-backed views and intrusive links whose
    // destructor is a no-op.  Do not register one disposer per slot: deleting
    // the pool is the destruction operation for the whole live set.
    auto* extra = new (pool_->allocate(sizeof(CellExtra))) CellExtra();
    allocatedExtraBytes_ += sizeof(CellExtra);
    extra->underlineColor = spec.underlineColor;
    if (spec.hasGrapheme()) {
        extra->graphemeBytes = copyBytes(StringView(
            reinterpret_cast<const u8*>(spec.grapheme.data()),
            spec.grapheme.size() * sizeof(u32)));
    }
    if (spec.hasHyperlink()) {
        StringView identity = spec.hyperlinkIdentity;
        StringView payload = spec.hyperlinkPayload;
        u32 displayId = spec.hyperlinkDisplayId;
        if (knownHyperlink != 0) {
            const HyperlinkHandle* canonical = hyperlinkOf(*slots_[knownHyperlink]);
            assert(canonical != nullptr);
            assert(equalBytes(canonical->identity, identity));
            assert(equalBytes(canonical->payload, payload));
            assert(canonical->displayId == displayId);
            identity = canonical->identity;
            payload = canonical->payload;
            displayId = canonical->displayId;
        } else {
            identity = copyBytes(identity);
            payload = copyBytes(payload);
        }
        auto* hyperlink = new (pool_->allocate(sizeof(HyperlinkHandle))) HyperlinkHandle(
            identity, payload, displayId);
        allocatedExtraBytes_ += sizeof(HyperlinkHandle);
        extra->hyperlinks.pushBack(hyperlink);
    }

    const u32 ref = static_cast<u32>(slots_.length());
    slots_.pushBack(extra);
    ++allocationsSinceGc_;

    if (newHyperlink) {
        ++hyperlinkCount_;
        const size_t hyperlinkBucket = spec.hyperlinkIdentity.hash64() % hyperlinkBuckets_.length();
        HyperlinkHandle* hyperlink = hyperlinkOf(*extra);
        hyperlink->identityNext = hyperlinkBuckets_[hyperlinkBucket];
        hyperlinkBuckets_.mut(hyperlinkBucket) = ref;
    }
    return ref;
}

u32 CellExtraStore::migrate(const CellExtraStore& source, u32 sourceRef) {
    // Copying GC preserves ref identity: one destination slot per distinct
    // live source ref, irrespective of equal payloads in other source slots.
    return append(source.describe(sourceRef));
}

CellColor CellExtraStore::underlineColor(const TerminalCell& cell) const noexcept {
    if (!cell.hasExtra()) {
        return cell.inlineUnderlineColor();
    }
    const CellExtra* extra = get(cell.extraRef());
    return extra == nullptr ? CellColor::defaultForeground() : extra->underlineColor;
}

GraphemeView CellExtraStore::grapheme(const TerminalCell& cell) const noexcept {
    return cell.hasExtra() ? grapheme(cell.extraRef()) : GraphemeView{};
}

GraphemeView CellExtraStore::grapheme(u32 ref) const noexcept {
    const CellExtra* extra = get(ref);
    return extra == nullptr ? GraphemeView{} : graphemeOf(*extra);
}

StringView CellExtraStore::hyperlink(const TerminalCell& cell) const noexcept {
    if (!cell.hasExtra()) {
        return {};
    }
    const CellExtra* extra = get(cell.extraRef());
    const HyperlinkHandle* handle = extra == nullptr ? nullptr : hyperlinkOf(*extra);
    return handle == nullptr ? StringView{} : handle->payload;
}

u32 CellExtraStore::hyperlinkDisplayId(const TerminalCell& cell) const noexcept {
    if (!cell.hasExtra()) {
        return 0;
    }
    const CellExtra* extra = get(cell.extraRef());
    const HyperlinkHandle* handle = extra == nullptr ? nullptr : hyperlinkOf(*extra);
    return handle == nullptr ? 0 : handle->displayId;
}

u32 CellExtraStore::findHyperlink(StringView identity) const noexcept {
    if (identity.empty()) {
        return 0;
    }
    const size_t bucket = identity.hash64() % hyperlinkBuckets_.length();
    for (u32 ref = hyperlinkBuckets_[bucket]; ref != 0;) {
        const HyperlinkHandle* handle = hyperlinkOf(*slots_[ref]);
        if (equalBytes(handle->identity, identity)) {
            return ref;
        }
        ref = handle->identityNext;
    }
    return 0;
}

u32 CellExtraStore::getOrCreateHyperlink(StringView identity, StringView payload, u32 displayId) {
    if (const u32 existing = findHyperlink(identity); existing != 0) {
        return existing;
    }
    CellExtraSpec spec;
    spec.hyperlinkIdentity = identity;
    spec.hyperlinkPayload = payload;
    spec.hyperlinkDisplayId = displayId;
    return append(spec);
}

void CellExtraStore::apply(TerminalCell& cell, const CellExtraSpec& spec) {
    if (!spec.needsExtra()) {
        cell.setInlineUnderlineColor(spec.underlineColor);
        return;
    }
    cell.setExtraRef(append(spec));
}

void CellExtraStore::setUnderlineColor(TerminalCell& cell, CellColor color) {
    CellExtraSpec spec = cell.hasExtra() ? describe(cell.extraRef()) : CellExtraSpec{};
    spec.underlineColor = color;
    apply(cell, spec);
}

void CellExtraStore::setGrapheme(TerminalCell& cell, const u32* codepoints, size_t count) {
    CellExtraSpec spec = cell.hasExtra() ? describe(cell.extraRef()) : CellExtraSpec{};
    if (!cell.hasExtra()) {
        spec.underlineColor = cell.inlineUnderlineColor();
    }
    spec.grapheme = {codepoints, static_cast<u32>(count)};
    apply(cell, spec);
}

void CellExtraStore::clearGrapheme(TerminalCell& cell) {
    if (!cell.hasExtra()) {
        return;
    }
    CellExtraSpec spec = describe(cell.extraRef());
    spec.grapheme = {};
    apply(cell, spec);
}

void CellExtraStore::setHyperlink(TerminalCell& cell, u32 hyperlinkRef) {
    if (hyperlinkRef == 0 && !cell.hasExtra()) {
        return;
    }

    CellExtraSpec spec = cell.hasExtra() ? describe(cell.extraRef()) : CellExtraSpec{};
    if (!cell.hasExtra()) {
        spec.underlineColor = cell.inlineUnderlineColor();
    }
    if (hyperlinkRef == 0) {
        spec.hyperlinkIdentity = {};
        spec.hyperlinkPayload = {};
        spec.hyperlinkDisplayId = 0;
    } else {
        const CellExtraSpec hyperlink = describe(hyperlinkRef);
        if (!cell.hasExtra()
            && hyperlink.hasHyperlink()
            && !hyperlink.hasGrapheme()
            && hyperlink.underlineColor == spec.underlineColor) {
            cell.setExtraRef(hyperlinkRef);
            return;
        }
        spec.hyperlinkIdentity = hyperlink.hyperlinkIdentity;
        spec.hyperlinkPayload = hyperlink.hyperlinkPayload;
        spec.hyperlinkDisplayId = hyperlink.hyperlinkDisplayId;
    }
    apply(cell, spec);
}

void CellExtraStore::clearHyperlink(TerminalCell& cell) {
    setHyperlink(cell, 0);
}

void CellExtraStore::clearExtra(TerminalCell& cell, CellColor underlineColor_) {
    cell.setInlineUnderlineColor(underlineColor_);
}

void CellExtraStore::setCellCount(size_t cellCount) noexcept {
    cellCount_ = std::max<size_t>(cellCount, 1);
    slotBudget_ = std::max<size_t>(16, cellCount_ * 10);
    allocationBudget_ = std::max<size_t>(16, cellCount_ * 2);
    byteBudget_ = std::max<size_t>(4096, cellCount_ * 64);
}

void CellExtraStore::finishCollection() noexcept {
    allocationsSinceGc_ = 0;
    slotBudget_ = std::max(slotBudget_, slots_.length() + cellCount_ * 2);
    byteBudget_ = std::max(
        byteBudget_,
        allocatedExtraBytes_ + std::max<size_t>(1024 * 1024, cellCount_ * 16));
}

bool CellExtraStore::shouldCollect() const noexcept {
    return allocatedExtraBytes_ > byteBudget_
        || slots_.length() > slotBudget_
        || allocationsSinceGc_ > allocationBudget_;
}

bool CellExtraStore::hardLimitExceeded() const noexcept {
    return allocatedExtraBytes_ > byteBudget_ * 2 || slots_.length() > slotBudget_ * 2;
}
