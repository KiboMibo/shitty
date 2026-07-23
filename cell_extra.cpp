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
    if (extra.hyperlinks == nullptr) {
        return nullptr;
    }
    const IntrusiveNode* node = extra.hyperlinks->front();
    return node == extra.hyperlinks->end() ? nullptr : static_cast<const HyperlinkHandle*>(node);
}

HyperlinkHandle* CellExtraStore::hyperlinkOf(CellExtra& extra) noexcept {
    if (extra.hyperlinks == nullptr) {
        return nullptr;
    }
    IntrusiveNode* node = extra.hyperlinks->mutFront();
    return node == extra.hyperlinks->mutEnd() ? nullptr : static_cast<HyperlinkHandle*>(node);
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

u32 CellExtraStore::append(const CellExtra& value) {
    assert(!value.graphemeBytes.empty() || hyperlinkOf(value) != nullptr);
    if (slots_.length() >= std::numeric_limits<u32>::max()) {
        throw std::bad_alloc();
    }

    auto* extra = pool_->make<CellExtra>(value);
    allocatedExtraBytes_ += sizeof(CellExtra);

    const u32 ref = static_cast<u32>(slots_.length());
    slots_.pushBack(extra);
    ++allocationsSinceGc_;
    return ref;
}

u32 CellExtraStore::migrate(const CellExtraStore& source, u32 sourceRef) {
    const CellExtra* sourceExtra = source.get(sourceRef);
    assert(sourceExtra != nullptr);

    CellExtra copy = *sourceExtra;
    if (!sourceExtra->graphemeBytes.empty()) {
        copy.graphemeBytes = copyBytes(sourceExtra->graphemeBytes);
    }

    bool indexHyperlink = false;
    if (const HyperlinkHandle* sourceHyperlink = hyperlinkOf(*sourceExtra);
        sourceHyperlink != nullptr) {
        if (const u32 known = findHyperlink(sourceHyperlink->identity); known != 0) {
            const HyperlinkHandle* canonical = hyperlinkOf(*slots_[known]);
            assert(canonical != nullptr);
            assert(equalBytes(canonical->payload, sourceHyperlink->payload));
            assert(canonical->displayId == sourceHyperlink->displayId);
            copy.hyperlinks = slots_[known]->hyperlinks;
        } else {
            if ((hyperlinkCount_ + 1) * 10 >= hyperlinkBuckets_.length() * 7) {
                rehashHyperlinks(hyperlinkBuckets_.length() * 2);
            }

            copy.hyperlinks = pool_->make<IntrusiveList>();
            allocatedExtraBytes_ += sizeof(IntrusiveList);
            for (const IntrusiveNode* node = sourceExtra->hyperlinks->front();
                 node != sourceExtra->hyperlinks->end(); node = node->next) {
                const auto* sourceHandle = static_cast<const HyperlinkHandle*>(node);
                const StringView identity = copyBytes(sourceHandle->identity);
                const StringView payload = copyBytes(sourceHandle->payload);
                auto* handle = pool_->make<HyperlinkHandle>(
                    identity, payload, sourceHandle->displayId);
                allocatedExtraBytes_ += sizeof(HyperlinkHandle);
                copy.hyperlinks->pushBack(handle);
            }
            indexHyperlink = true;
        }
    } else {
        copy.hyperlinks = nullptr;
    }

    // Copying GC preserves ref identity: one destination slot per distinct
    // live source ref, irrespective of equal payloads in other source slots.
    const u32 ref = append(copy);
    if (indexHyperlink) {
        ++hyperlinkCount_;
        HyperlinkHandle* hyperlink = hyperlinkOf(*slots_[ref]);
        const size_t bucket = hyperlink->identity.hash64() % hyperlinkBuckets_.length();
        hyperlink->identityNext = hyperlinkBuckets_[bucket];
        hyperlinkBuckets_.mut(bucket) = ref;
    }
    return ref;
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
    if ((hyperlinkCount_ + 1) * 10 >= hyperlinkBuckets_.length() * 7) {
        rehashHyperlinks(hyperlinkBuckets_.length() * 2);
    }

    CellExtra extra;
    extra.hyperlinks = pool_->make<IntrusiveList>();
    allocatedExtraBytes_ += sizeof(IntrusiveList);
    const StringView storedIdentity = copyBytes(identity);
    const StringView storedPayload = copyBytes(payload);
    auto* hyperlink = pool_->make<HyperlinkHandle>(
        storedIdentity, storedPayload, displayId);
    allocatedExtraBytes_ += sizeof(HyperlinkHandle);
    extra.hyperlinks->pushBack(hyperlink);

    const u32 ref = append(extra);
    ++hyperlinkCount_;
    const size_t bucket = storedIdentity.hash64() % hyperlinkBuckets_.length();
    hyperlink->identityNext = hyperlinkBuckets_[bucket];
    hyperlinkBuckets_.mut(bucket) = ref;
    return ref;
}

void CellExtraStore::setUnderlineColor(TerminalCell& cell, CellColor color) {
    if (!cell.hasExtra()) {
        cell.setInlineUnderlineColor(color);
        return;
    }

    const CellExtra* current = get(cell.extraRef());
    assert(current != nullptr);
    CellExtra copy = *current;
    copy.underlineColor = color;
    cell.setExtraRef(append(copy));
}

void CellExtraStore::setGrapheme(TerminalCell& cell, const u32* codepoints, size_t count) {
    if (count == 0) {
        clearGrapheme(cell);
        return;
    }

    CellExtra copy;
    if (cell.hasExtra()) {
        const CellExtra* current = get(cell.extraRef());
        assert(current != nullptr);
        copy = *current;
    } else {
        copy.underlineColor = cell.inlineUnderlineColor();
    }
    copy.graphemeBytes = copyBytes(StringView(
        reinterpret_cast<const u8*>(codepoints), count * sizeof(u32)));
    cell.setExtraRef(append(copy));
}

void CellExtraStore::clearGrapheme(TerminalCell& cell) {
    if (!cell.hasExtra()) {
        return;
    }

    const CellExtra* current = get(cell.extraRef());
    assert(current != nullptr);
    if (current->graphemeBytes.empty()) {
        return;
    }
    if (hyperlinkOf(*current) == nullptr) {
        cell.setInlineUnderlineColor(current->underlineColor);
        return;
    }

    CellExtra copy = *current;
    copy.graphemeBytes = {};
    cell.setExtraRef(append(copy));
}

void CellExtraStore::setHyperlink(TerminalCell& cell, u32 hyperlinkRef) {
    const CellExtra* hyperlinkExtra = get(hyperlinkRef);
    if (hyperlinkExtra == nullptr || hyperlinkOf(*hyperlinkExtra) == nullptr) {
        clearHyperlink(cell);
        return;
    }

    CellExtra copy;
    if (cell.hasExtra()) {
        const CellExtra* current = get(cell.extraRef());
        assert(current != nullptr);
        if (current->hyperlinks == hyperlinkExtra->hyperlinks) {
            return;
        }
        copy = *current;
    } else {
        copy.underlineColor = cell.inlineUnderlineColor();
        if (hyperlinkExtra->graphemeBytes.empty()
            && hyperlinkExtra->underlineColor == copy.underlineColor) {
            cell.setExtraRef(hyperlinkRef);
            return;
        }
    }
    copy.hyperlinks = hyperlinkExtra->hyperlinks;
    cell.setExtraRef(append(copy));
}

void CellExtraStore::clearHyperlink(TerminalCell& cell) {
    if (!cell.hasExtra()) {
        return;
    }

    const CellExtra* current = get(cell.extraRef());
    assert(current != nullptr);
    if (hyperlinkOf(*current) == nullptr) {
        return;
    }
    if (current->graphemeBytes.empty()) {
        cell.setInlineUnderlineColor(current->underlineColor);
        return;
    }

    CellExtra copy = *current;
    copy.hyperlinks = nullptr;
    cell.setExtraRef(append(copy));
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
