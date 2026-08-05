/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "darts.h"

namespace {
    struct TrieNode {
        i32 first = 0;
        i32 parent = 0;
        i32 key = 0;
        i32 below = 0;
        i32 lone = 0;
    };

    struct TrieEdge {
        i32 next = 0;
        i32 child = 0;
        u8 code = 0;
    };

    struct Placement {
        i32 node = 0;
        i32 state = 0;
    };

    // The subtree summary for one state: the key index + 1 when exactly
    // one key lives below, -1 when several do, 0 when none.
    static i32 loneMark(const TrieNode& node) noexcept {
        if (node.below > 1) {
            return -1;
        }

        return node.lone;
    }
}

void Darts::build(const stl::StringView* keys, size_t count) {
    stl::Vector<TrieNode> nodes;
    stl::Vector<TrieEdge> edges;

    nodes.pushBack({});
    edges.pushBack({});

    for (size_t index = 0; index < count; ++index) {
        i32 at = 0;

        for (const u8 code : keys[index]) {
            i32 child = 0;

            for (i32 edge = nodes[at].first; edge != 0; edge = edges[edge].next) {
                if (edges[edge].code == code) {
                    child = edges[edge].child;

                    break;
                }
            }

            if (child == 0) {
                nodes.pushBack({0, at, 0, 0, 0});
                child = (i32)(nodes.length()) - 1;
                edges.pushBack({nodes[at].first, child, code});
                nodes.mutData()[at].first = (i32)(edges.length()) - 1;
            }

            at = child;
        }

        nodes.mutData()[at].key = (i32)(index) + 1;
    }

    // Children are created after their parents, so one reverse pass
    // completes every subtree count before its parent consumes it.
    for (i32 at = (i32)(nodes.length()) - 1; at >= 0; --at) {
        TrieNode& node = nodes.mutData()[at];

        if (node.key != 0) {
            node.below += 1;
            node.lone = node.key;
        }

        if (at != 0) {
            TrieNode& parent = nodes.mutData()[node.parent];

            parent.below += node.below;

            if (node.below != 0) {
                parent.lone = node.lone;
            }
        }
    }

    base_.clear();
    check_.clear();
    value_.clear();
    lone_.clear();
    ensure(0);
    value_.mutData()[0] = nodes[0].key;
    lone_.mutData()[0] = loneMark(nodes[0]);

    stl::Vector<Placement> queue;

    queue.pushBack({0, 0});

    i32 baseHint = 1;

    for (size_t head = 0; head < queue.length(); ++head) {
        const Placement item = queue[head];
        const TrieNode& node = nodes[item.node];

        if (node.first == 0) {
            continue;
        }

        i32 codes[256];
        i32 children[256];
        size_t fan = 0;
        i32 maxCode = 0;

        for (i32 edge = node.first; edge != 0; edge = edges[edge].next) {
            codes[fan] = (i32)(edges[edge].code) + 1;

            if (codes[fan] > maxCode) {
                maxCode = codes[fan];
            }

            children[fan] = edges[edge].child;
            fan += 1;
        }

        i32 found = baseHint;

        for (;; ++found) {
            ensure(found + maxCode);

            bool free = true;

            for (size_t index = 0; index < fan; ++index) {
                if (check_[found + codes[index]] != 0) {
                    free = false;

                    break;
                }
            }

            if (free) {
                break;
            }
        }

        baseHint = found;
        base_.mutData()[item.state] = found;

        for (size_t index = 0; index < fan; ++index) {
            const i32 state = found + codes[index];

            check_.mutData()[state] = item.state + 1;
            value_.mutData()[state] = nodes[children[index]].key;
            lone_.mutData()[state] = loneMark(nodes[children[index]]);
            queue.pushBack({children[index], state});
        }
    }
}

void Darts::ensure(i32 top) {
    while ((i32)(base_.length()) <= top) {
        base_.pushBack(0);
        check_.pushBack(0);
        value_.pushBack(0);
        lone_.pushBack(0);
    }
}

i32 Darts::walk(stl::StringView text) const noexcept {
    if (base_.empty()) {
        return -1;
    }

    i32 state = 0;

    for (const u8 byte : text) {
        const i32 next = base_[state] + (i32)(byte) + 1;

        if (next >= (i32)(check_.length()) || check_[next] != state + 1) {
            return -1;
        }

        state = next;
    }

    return state;
}

i32 Darts::find(stl::StringView key) const noexcept {
    const i32 state = walk(key);

    if (state < 0 || value_[state] == 0) {
        return missing;
    }

    return value_[state] - 1;
}

i32 Darts::resolve(stl::StringView prefix) const noexcept {
    const i32 state = walk(prefix);

    if (state < 0) {
        return missing;
    }

    if (value_[state] != 0) {
        return value_[state] - 1;
    }

    if (lone_[state] > 0) {
        return lone_[state] - 1;
    }

    if (lone_[state] < 0) {
        return ambiguous;
    }

    return missing;
}
