/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "render_damage.h"

#include <std/dbg/assert.h>

static_assert(sizeof(RenderDamage::Entry) == 16);

void RenderDamage::configure(Entry* storage_, u32 capacity_) noexcept {
    STD_ASSERT(count == 0);
    storage = storage_;
    capacity = capacity_;
}

bool RenderDamage::advance() noexcept {
    if (++generation != 0) {
        return false;
    }
    generation = 1;
    fullGeneration = 1;
    begin = 0;
    count = 0;
    return true;
}

void RenderDamage::add(u32 first, u32 length) noexcept {
    if (length == 0 || fullGeneration == generation) {
        return;
    }
    if (count == capacity) {
        full();
        return;
    }
    if (count != 0) {
        Entry& previous = storage[(begin + count - 1) % capacity];
        if (previous.generation == generation && previous.begin + previous.count == first) {
            previous.count += length;
            return;
        }
    }
    storage[(begin + count) % capacity] = {generation, first, length};
    ++count;
}

void RenderDamage::full() noexcept {
    fullGeneration = generation;
    begin = 0;
    count = 0;
}

void RenderDamage::collect(u64 appliedGeneration) noexcept {
    while (count != 0 && storage[begin].generation <= appliedGeneration) {
        begin = (begin + 1) % capacity;
        --count;
    }
    if (count == 0) {
        begin = 0;
    }
}

bool RenderDamage::requiresFull(u64 appliedGeneration, bool initialized) const noexcept {
    return !initialized || appliedGeneration < fullGeneration;
}

const RenderDamage::Entry& RenderDamage::entry(u32 index) const noexcept {
    STD_ASSERT(index < count);
    return storage[(begin + index) % capacity];
}
