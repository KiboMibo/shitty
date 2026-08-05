#pragma once

#include <std/lib/vector.h>
#include <std/str/view.h>

namespace stl {
    // A double-array trie over byte keys. build() maps every key to its
    // position in the input; the queries then cost one array probe per
    // input byte, with no per-key scanning. Only the automaton is kept,
    // the key bytes themselves are not stored.
    class Darts {
        Vector<i32> base_;
        Vector<i32> check_;
        Vector<i32> value_;
        Vector<i32> lone_;

        i32 walk(StringView text) const noexcept;
        void ensure(i32 top);

    public:
        static constexpr i32 missing = -1;
        static constexpr i32 ambiguous = -2;

        void build(const StringView* keys, size_t count);

        // The index of the exactly matching key, or missing.
        i32 find(StringView key) const noexcept;

        // Completion: an exact key wins, otherwise the single key the
        // prefix begins; missing or ambiguous otherwise.
        i32 resolve(StringView prefix) const noexcept;
    };
}
