/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "cell_extra_store.h"

#include "composer.h"

#include <std/lib/list.h>
#include <std/mem/obj_pool.h>
#include <std/str/view.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>
#include <new>

using namespace stl;

namespace {
    struct HyperlinkHandle final: IntrusiveNode {
        StringView identity;
        StringView payload;
        u32 displayId = 0;
        u32 identityNext = 0;

        HyperlinkHandle(StringView identity_, StringView payload_, u32 displayId_) noexcept;
    };

    struct CellExtra {
        IntrusiveList* hyperlinks = nullptr;
        StringView graphemeBytes;
        CellColor underlineColor = CellColor::defaultForeground();
    };

    struct CellExtraStoreOwner {
        ~CellExtraStoreOwner() noexcept;

        ObjPool* pool = nullptr;
    };

    static_assert(__is_trivially_copyable(CellExtra), "CellExtra must remain trivially copyable");

    struct CellExtraStoreImpl final: public CellExtraStore {
        CellExtraStoreImpl(Composer& composer, size_t cellCount, CellExtraStoreOwner& owner, ObjPool* pool);

        static CellExtraStoreImpl* create(Composer& composer, size_t cellCount, CellExtraStoreOwner& owner);

        CellColor underlineColor(const TerminalCell& cell) const noexcept override;
        GraphemeView grapheme(const TerminalCell& cell) const noexcept override;
        GraphemeView grapheme(u32 ref) const noexcept override;
        StringView hyperlink(const TerminalCell& cell) const noexcept override;
        u32 hyperlinkDisplayId(const TerminalCell& cell) const noexcept override;

        u32 getOrCreateHyperlink(StringView identity, StringView payload, u32 displayId) override;
        u32 findHyperlink(StringView identity) const noexcept override;
        size_t hyperlinkCount() const noexcept override;

        void setUnderlineColor(TerminalCell& cell, CellColor color) override;
        void setGrapheme(TerminalCell& cell, const u32* codepoints, size_t count) override;
        void clearGrapheme(TerminalCell& cell) override;
        void setHyperlink(TerminalCell& cell, u32 hyperlinkRef) override;
        void clearHyperlink(TerminalCell& cell) override;
        void clearExtra(TerminalCell& cell, CellColor underlineColor) override;

        void setCellCount(size_t cellCount) noexcept override;
        bool shouldCollect() const noexcept override;
        bool hardLimitExceeded() const noexcept override;
        void collect(Vector<u32*>& locations) override;

        Composer& composer_;
        CellExtraStoreOwner& owner_;
        ObjPool* pool_;
        Vector<CellExtra*> slots_;
        // OSC 8 identity lookup only. This is not a CellExtra content index:
        // append() always creates a new dense ref.
        Vector<u32> hyperlinkBuckets_;
        size_t allocatedExtraBytes_ = 256;
        size_t allocationsSinceGc_ = 0;
        size_t cellCount_ = 0;
        size_t byteBudget_ = 0;
        size_t slotBudget_ = 0;
        size_t allocationBudget_ = 0;
        size_t hyperlinkCount_ = 0;

        const CellExtra* get(u32 ref) const noexcept;
        u32 migrate(const CellExtraStoreImpl& source, u32 sourceRef);
        void finishCollection() noexcept;

        static GraphemeView graphemeOf(const CellExtra& extra) noexcept;
        static HyperlinkHandle* hyperlinkOf(CellExtra& extra) noexcept;
        static const HyperlinkHandle* hyperlinkOf(const CellExtra& extra) noexcept;

        u32 append(const CellExtra& extra);
        void rehashHyperlinks(size_t capacity);
        StringView copyBytes(StringView value);
    };

    bool equalBytes(StringView left, StringView right) noexcept {
        return left.length() == right.length() && (left.empty() || memcmp(left.data(), right.data(), left.length()) == 0);
    }
}

CellExtraStoreOwner::~CellExtraStoreOwner() noexcept {
    delete pool;
}

HyperlinkHandle::HyperlinkHandle(StringView identity_, StringView payload_, u32 displayId_) noexcept
    : identity(identity_)
    , payload(payload_)
    , displayId(displayId_) {
}

CellExtraStoreImpl::CellExtraStoreImpl(Composer& composer, size_t cellCount, CellExtraStoreOwner& owner, ObjPool* pool)
    : composer_(composer)
    , owner_(owner)
    , pool_(pool)
{
    slots_.pushBack(nullptr);
    rehashHyperlinks(16);
    setCellCount(cellCount);
}

CellExtraStoreImpl* CellExtraStoreImpl::create(Composer& composer, size_t cellCount, CellExtraStoreOwner& owner) {
    ObjPool* pool = ObjPool::fromMemoryRaw();
    try {
        auto* result = pool->make<CellExtraStoreImpl>(composer, cellCount, owner, pool);
        if (owner.pool == nullptr) {
            owner.pool = pool;
        }
        return result;
    } catch (...) {
        delete pool;
        throw;
    }
}

size_t CellExtraStoreImpl::hyperlinkCount() const noexcept {
    return hyperlinkCount_;
}

const CellExtra* CellExtraStoreImpl::get(u32 ref) const noexcept {
    if (ref == 0 || ref >= slots_.length()) {
        return nullptr;
    }
    return slots_[ref];
}

GraphemeView CellExtraStoreImpl::graphemeOf(const CellExtra& extra) noexcept {
    return {
        reinterpret_cast<const u32*>(extra.graphemeBytes.data()),
        static_cast<u32>(extra.graphemeBytes.length() / sizeof(u32)),
    };
}

const HyperlinkHandle* CellExtraStoreImpl::hyperlinkOf(const CellExtra& extra) noexcept {
    if (extra.hyperlinks == nullptr) {
        return nullptr;
    }
    return static_cast<const HyperlinkHandle*>(extra.hyperlinks->front());
}

HyperlinkHandle* CellExtraStoreImpl::hyperlinkOf(CellExtra& extra) noexcept {
    if (extra.hyperlinks == nullptr) {
        return nullptr;
    }
    return static_cast<HyperlinkHandle*>(extra.hyperlinks->mutFront());
}

StringView CellExtraStoreImpl::copyBytes(StringView value) {
    if (value.empty()) {
        return {};
    }
    allocatedExtraBytes_ += value.length() + 1;
    return pool_->intern(value);
}

void CellExtraStoreImpl::rehashHyperlinks(size_t capacity) {
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

u32 CellExtraStoreImpl::append(const CellExtra& value) {
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

u32 CellExtraStoreImpl::migrate(const CellExtraStoreImpl& source, u32 sourceRef) {
    const CellExtra& sourceExtra = *source.get(sourceRef);

    CellExtra copy = sourceExtra;
    if (!sourceExtra.graphemeBytes.empty()) {
        copy.graphemeBytes = copyBytes(sourceExtra.graphemeBytes);
    }

    bool indexHyperlink = false;
    if (const HyperlinkHandle* sourceHyperlink = hyperlinkOf(sourceExtra); sourceHyperlink != nullptr) {
        if (const u32 known = findHyperlink(sourceHyperlink->identity); known != 0) {
            const HyperlinkHandle& canonical = *hyperlinkOf(*slots_[known]);
            assert(equalBytes(canonical.payload, sourceHyperlink->payload));
            assert(canonical.displayId == sourceHyperlink->displayId);
            copy.hyperlinks = slots_[known]->hyperlinks;
        } else {
            if ((hyperlinkCount_ + 1) * 10 >= hyperlinkBuckets_.length() * 7) {
                rehashHyperlinks(hyperlinkBuckets_.length() * 2);
            }

            copy.hyperlinks = pool_->make<IntrusiveList>();
            allocatedExtraBytes_ += sizeof(IntrusiveList);
            for (const IntrusiveNode* node = sourceExtra.hyperlinks->front(); node != sourceExtra.hyperlinks->end(); node = node->next) {
                const auto* sourceHandle = static_cast<const HyperlinkHandle*>(node);
                const StringView identity = copyBytes(sourceHandle->identity);
                const StringView payload = copyBytes(sourceHandle->payload);
                auto* handle = pool_->make<HyperlinkHandle>(identity, payload, sourceHandle->displayId);
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

CellColor CellExtraStoreImpl::underlineColor(const TerminalCell& cell) const noexcept {
    if (!cell.hasExtra()) {
        return cell.inlineUnderlineColor();
    }
    return get(cell.extraRef())->underlineColor;
}

GraphemeView CellExtraStoreImpl::grapheme(const TerminalCell& cell) const noexcept {
    return cell.hasExtra() ? grapheme(cell.extraRef()) : GraphemeView{};
}

GraphemeView CellExtraStoreImpl::grapheme(u32 ref) const noexcept {
    const CellExtra* extra = get(ref);
    return extra == nullptr ? GraphemeView{} : graphemeOf(*extra);
}

StringView CellExtraStoreImpl::hyperlink(const TerminalCell& cell) const noexcept {
    if (!cell.hasExtra()) {
        return {};
    }
    const HyperlinkHandle* handle = hyperlinkOf(*get(cell.extraRef()));
    return handle == nullptr ? StringView{} : handle->payload;
}

u32 CellExtraStoreImpl::hyperlinkDisplayId(const TerminalCell& cell) const noexcept {
    if (!cell.hasExtra()) {
        return 0;
    }
    const HyperlinkHandle* handle = hyperlinkOf(*get(cell.extraRef()));
    return handle == nullptr ? 0 : handle->displayId;
}

u32 CellExtraStoreImpl::findHyperlink(StringView identity) const noexcept {
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

u32 CellExtraStoreImpl::getOrCreateHyperlink(StringView identity, StringView payload, u32 displayId) {
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
    auto* hyperlink = pool_->make<HyperlinkHandle>(storedIdentity, storedPayload, displayId);
    allocatedExtraBytes_ += sizeof(HyperlinkHandle);
    extra.hyperlinks->pushBack(hyperlink);

    const u32 ref = append(extra);
    ++hyperlinkCount_;
    const size_t bucket = storedIdentity.hash64() % hyperlinkBuckets_.length();
    hyperlink->identityNext = hyperlinkBuckets_[bucket];
    hyperlinkBuckets_.mut(bucket) = ref;
    return ref;
}

void CellExtraStoreImpl::setUnderlineColor(TerminalCell& cell, CellColor color) {
    if (!cell.hasExtra()) {
        cell.setInlineUnderlineColor(color);
        return;
    }

    CellExtra copy = *get(cell.extraRef());
    copy.underlineColor = color;
    cell.setExtraRef(append(copy));
}

void CellExtraStoreImpl::setGrapheme(TerminalCell& cell, const u32* codepoints, size_t count) {
    if (count == 0) {
        clearGrapheme(cell);
        return;
    }

    CellExtra copy;
    if (cell.hasExtra()) {
        copy = *get(cell.extraRef());
    } else {
        copy.underlineColor = cell.inlineUnderlineColor();
    }
    copy.graphemeBytes = copyBytes(StringView(reinterpret_cast<const u8*>(codepoints), count * sizeof(u32)));
    cell.setExtraRef(append(copy));
}

void CellExtraStoreImpl::clearGrapheme(TerminalCell& cell) {
    if (!cell.hasExtra()) {
        return;
    }

    const CellExtra& current = *get(cell.extraRef());
    if (current.graphemeBytes.empty()) {
        return;
    }
    if (hyperlinkOf(current) == nullptr) {
        cell.setInlineUnderlineColor(current.underlineColor);
        return;
    }

    CellExtra copy = current;
    copy.graphemeBytes = {};
    cell.setExtraRef(append(copy));
}

void CellExtraStoreImpl::setHyperlink(TerminalCell& cell, u32 hyperlinkRef) {
    if (hyperlinkRef == 0) {
        clearHyperlink(cell);
        return;
    }
    const CellExtra& hyperlinkExtra = *get(hyperlinkRef);
    if (hyperlinkOf(hyperlinkExtra) == nullptr) {
        clearHyperlink(cell);
        return;
    }

    CellExtra copy;
    if (cell.hasExtra()) {
        const CellExtra& current = *get(cell.extraRef());
        if (current.hyperlinks == hyperlinkExtra.hyperlinks) {
            return;
        }
        copy = current;
    } else {
        copy.underlineColor = cell.inlineUnderlineColor();
        if (hyperlinkExtra.graphemeBytes.empty() && hyperlinkExtra.underlineColor == copy.underlineColor) {
            cell.setExtraRef(hyperlinkRef);
            return;
        }
    }
    copy.hyperlinks = hyperlinkExtra.hyperlinks;
    cell.setExtraRef(append(copy));
}

void CellExtraStoreImpl::clearHyperlink(TerminalCell& cell) {
    if (!cell.hasExtra()) {
        return;
    }

    const CellExtra& current = *get(cell.extraRef());
    if (hyperlinkOf(current) == nullptr) {
        return;
    }
    if (current.graphemeBytes.empty()) {
        cell.setInlineUnderlineColor(current.underlineColor);
        return;
    }

    CellExtra copy = current;
    copy.hyperlinks = nullptr;
    cell.setExtraRef(append(copy));
}

void CellExtraStoreImpl::clearExtra(TerminalCell& cell, CellColor underlineColor_) {
    cell.setInlineUnderlineColor(underlineColor_);
}

void CellExtraStoreImpl::setCellCount(size_t cellCount) noexcept {
    cellCount_ = std::max<size_t>(cellCount, 1);
    slotBudget_ = std::max<size_t>(16, cellCount_ * 10);
    allocationBudget_ = std::max<size_t>(16, cellCount_ * 2);
    byteBudget_ = std::max<size_t>(4096, cellCount_ * 64);
}

void CellExtraStoreImpl::finishCollection() noexcept {
    allocationsSinceGc_ = 0;
    slotBudget_ = std::max(slotBudget_, slots_.length() + cellCount_ * 2);
    byteBudget_ = std::max(byteBudget_, allocatedExtraBytes_ + std::max<size_t>(1024 * 1024, cellCount_ * 16));
}

bool CellExtraStoreImpl::shouldCollect() const noexcept {
    return allocatedExtraBytes_ > byteBudget_ || slots_.length() > slotBudget_ || allocationsSinceGc_ > allocationBudget_;
}

bool CellExtraStoreImpl::hardLimitExceeded() const noexcept {
    return allocatedExtraBytes_ > byteBudget_ * 2 || slots_.length() > slotBudget_ * 2;
}

void CellExtraStoreImpl::collect(Vector<u32*>& locations) {
    assert(composer_.cellExtras == this);

    auto* next = CellExtraStoreImpl::create(composer_, cellCount_, owner_);
    try {
        Vector<u32> relocation;
        relocation.zero(slots_.length());

        for (u32* location : locations) {
            const u32 oldRef = *location;
            assert(oldRef != 0 && oldRef < slots_.length());
            if (relocation[oldRef] == 0) {
                // migrate() always appends. Equal but distinct old refs must
                // not collapse into one ref in the new store.
                relocation.mut(oldRef) = next->migrate(*this, oldRef);
            }
        }
        for (u32* location : locations) {
            *location = relocation[*location];
        }
        next->finishCollection();
    } catch (...) {
        delete next->pool_;
        throw;
    }

    ObjPool* oldPool = owner_.pool;
    assert(oldPool == pool_);
    composer_.setCellExtras(next);
    owner_.pool = next->pool_;
    delete oldPool;
}

CellExtraStore* CellExtraStore::create(Composer& composer, size_t cellCount) {
    assert(composer.cellExtras == nullptr);
    auto* owner = composer.pool->make<CellExtraStoreOwner>();
    auto* result = CellExtraStoreImpl::create(composer, cellCount, *owner);
    composer.setCellExtras(result);
    return result;
}
