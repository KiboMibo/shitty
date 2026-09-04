/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/str/view.h>

namespace stl {
    class ObjPool;
}

// A double-array trie over byte keys. create() maps every key to its
// position in the input; the queries then cost one array probe per
// input byte, with no per-key scanning. Only the automaton is kept,
// the key bytes themselves are not stored.
struct Darts {
    static constexpr i32 missing = -1;
    static constexpr i32 ambiguous = -2;

    virtual ~Darts() noexcept;

    // The index of the exactly matching key, or missing.
    virtual i32 find(stl::StringView key) const noexcept = 0;

    // Completion: an exact key wins, otherwise the single key the
    // prefix begins; missing or ambiguous otherwise.
    virtual i32 resolve(stl::StringView prefix) const noexcept = 0;

    static Darts* create(stl::ObjPool& pool, const stl::StringView* keys, size_t count);
};
